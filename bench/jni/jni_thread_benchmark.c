#define _POSIX_C_SOURCE 200809L

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define QUEUE_CAPACITY 1024

struct bench_context;

struct op_slot {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	uint64_t seq;
	uint64_t completed_seq;
	uint64_t bridge_start_ns;
	uint64_t result;
};

struct op_queue {
	struct op_slot *items[QUEUE_CAPACITY];
	size_t head;
	size_t tail;
	size_t size;
	int stopping;
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
};

struct bootstrap_context {
	JavaVM *jvm;
	jclass target_class;
	jmethodID consume_method;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int ready;
	int failed;
	int shutdown;
};

struct worker_context {
	struct bench_context *bench;
	int index;
	uint64_t iterations;
	uint64_t total_ns;
	uint64_t queue_wait_ns;
	uint64_t min_ns;
	uint64_t max_ns;
};

struct bridge_context {
	struct bench_context *bench;
};

struct bench_context {
	struct bootstrap_context bootstrap;
	struct op_queue queue;
	struct worker_context *workers;
	pthread_t *worker_threads;
	pthread_t *bridge_threads;
	int worker_count;
	int bridge_count;
	uint64_t iterations;
	int mode_bridge;
};

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}

	return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void die(const char *message)
{
	fprintf(stderr, "%s\n", message);
	exit(EXIT_FAILURE);
}

static void check_jni(JNIEnv *env, const char *where)
{
	if ((*env)->ExceptionCheck(env)) {
		fprintf(stderr, "JNI exception at %s\n", where);
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		die("JNI failure");
	}
}

static void queue_init(struct op_queue *queue)
{
	memset(queue, 0, sizeof(*queue));
	pthread_mutex_init(&queue->mutex, NULL);
	pthread_cond_init(&queue->not_empty, NULL);
	pthread_cond_init(&queue->not_full, NULL);
}

static void queue_stop(struct op_queue *queue)
{
	pthread_mutex_lock(&queue->mutex);
	queue->stopping = 1;
	pthread_cond_broadcast(&queue->not_empty);
	pthread_cond_broadcast(&queue->not_full);
	pthread_mutex_unlock(&queue->mutex);
}

static int queue_push(struct op_queue *queue, struct op_slot *slot)
{
	pthread_mutex_lock(&queue->mutex);
	while (!queue->stopping && queue->size == QUEUE_CAPACITY) {
		pthread_cond_wait(&queue->not_full, &queue->mutex);
	}
	if (queue->stopping) {
		pthread_mutex_unlock(&queue->mutex);
		return -1;
	}

	queue->items[queue->tail] = slot;
	queue->tail = (queue->tail + 1U) % QUEUE_CAPACITY;
	queue->size++;
	pthread_cond_signal(&queue->not_empty);
	pthread_mutex_unlock(&queue->mutex);
	return 0;
}

static struct op_slot *queue_pop(struct op_queue *queue)
{
	struct op_slot *slot;

	pthread_mutex_lock(&queue->mutex);
	while (!queue->stopping && queue->size == 0) {
		pthread_cond_wait(&queue->not_empty, &queue->mutex);
	}
	if (queue->size == 0) {
		pthread_mutex_unlock(&queue->mutex);
		return NULL;
	}

	slot = queue->items[queue->head];
	queue->head = (queue->head + 1U) % QUEUE_CAPACITY;
	queue->size--;
	pthread_cond_signal(&queue->not_full);
	pthread_mutex_unlock(&queue->mutex);
	return slot;
}

static void *bootstrap_main(void *arg)
{
	struct bootstrap_context *bootstrap = arg;
	JavaVMInitArgs vm_args;
	JavaVMOption option;
	JNIEnv *env = NULL;
	jclass local_class;
	char classpath[512];
	jint rc;

	if (snprintf(classpath, sizeof(classpath), "-Djava.class.path=%s", "build/classes") >= (int) sizeof(classpath)) {
		die("classpath too long");
	}

	option.optionString = classpath;
	vm_args.version = JNI_VERSION_21;
	vm_args.nOptions = 1;
	vm_args.options = &option;
	vm_args.ignoreUnrecognized = JNI_FALSE;

	rc = JNI_CreateJavaVM(&bootstrap->jvm, (void **) &env, &vm_args);

	pthread_mutex_lock(&bootstrap->mutex);
	if (rc != JNI_OK) {
		bootstrap->failed = 1;
		bootstrap->ready = 1;
		pthread_cond_broadcast(&bootstrap->cond);
		pthread_mutex_unlock(&bootstrap->mutex);
		return NULL;
	}

	local_class = (*env)->FindClass(env, "org/ckarta/bench/BenchmarkTarget");
	if (local_class == NULL) {
		bootstrap->failed = 1;
		bootstrap->ready = 1;
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		pthread_cond_broadcast(&bootstrap->cond);
		pthread_mutex_unlock(&bootstrap->mutex);
		return NULL;
	}

	bootstrap->target_class = (*env)->NewGlobalRef(env, local_class);
	(*env)->DeleteLocalRef(env, local_class);
	bootstrap->consume_method = (*env)->GetStaticMethodID(env, bootstrap->target_class, "consume", "(J)J");
	check_jni(env, "GetStaticMethodID");
	if (bootstrap->consume_method == NULL) {
		bootstrap->failed = 1;
	}

	bootstrap->ready = 1;
	pthread_cond_broadcast(&bootstrap->cond);
	pthread_mutex_unlock(&bootstrap->mutex);

	pthread_mutex_lock(&bootstrap->mutex);
	while (!bootstrap->shutdown) {
		pthread_cond_wait(&bootstrap->cond, &bootstrap->mutex);
	}
	pthread_mutex_unlock(&bootstrap->mutex);

	if (bootstrap->target_class != NULL) {
		(*env)->DeleteGlobalRef(env, bootstrap->target_class);
	}
	return NULL;
}

static int attach(JNIEnv **env_out, JavaVM *jvm)
{
	void *value = NULL;
	jint rc = (*jvm)->GetEnv(jvm, &value, JNI_VERSION_21);

	if (rc == JNI_OK) {
		*env_out = (JNIEnv *) value;
		return 0;
	}

	rc = (*jvm)->AttachCurrentThread(jvm, &value, NULL);
	if (rc != JNI_OK) {
		return -1;
	}

	*env_out = (JNIEnv *) value;
	return 1;
}

static void *bridge_main(void *arg)
{
	struct bridge_context *bridge = arg;
	struct bench_context *bench = bridge->bench;
	JNIEnv *env = NULL;
	int attached = attach(&env, bench->bootstrap.jvm);

	if (attached < 0) {
		die("AttachCurrentThread failed");
	}

	for (;;) {
		struct op_slot *slot = queue_pop(&bench->queue);
		uint64_t start;
		jlong result;

		if (slot == NULL) {
			break;
		}

		start = now_ns();
		result = (*env)->CallStaticLongMethod(env,
		                                     bench->bootstrap.target_class,
		                                     bench->bootstrap.consume_method,
		                                     (jlong) slot->seq);
		check_jni(env, "CallStaticLongMethod");

		pthread_mutex_lock(&slot->mutex);
		slot->result = (uint64_t) result;
		slot->bridge_start_ns = start;
		slot->completed_seq = slot->seq;
		pthread_cond_signal(&slot->cond);
		pthread_mutex_unlock(&slot->mutex);
	}

	if (attached > 0) {
		(*bench->bootstrap.jvm)->DetachCurrentThread(bench->bootstrap.jvm);
	}
	return NULL;
}

static void stats_add(struct worker_context *worker, uint64_t latency, uint64_t queue_wait)
{
	worker->total_ns += latency;
	worker->queue_wait_ns += queue_wait;
	if (worker->min_ns == 0 || latency < worker->min_ns) {
		worker->min_ns = latency;
	}
	if (latency > worker->max_ns) {
		worker->max_ns = latency;
	}
}

static void *worker_main(void *arg)
{
	struct worker_context *worker = arg;
	struct bench_context *bench = worker->bench;
	JNIEnv *env = NULL;
	int attached = 0;
	struct op_slot slot;

	if (!bench->mode_bridge) {
		attached = attach(&env, bench->bootstrap.jvm);
		if (attached < 0) {
			die("AttachCurrentThread failed");
		}
	}

	memset(&slot, 0, sizeof(slot));
	slot.completed_seq = UINT64_MAX;
	pthread_mutex_init(&slot.mutex, NULL);
	pthread_cond_init(&slot.cond, NULL);

	for (uint64_t i = 0; i < worker->iterations; i++) {
		uint64_t submitted;
		uint64_t completed;

		slot.seq = ((uint64_t) (worker->index + 1) << 48) | (i + 1);
		submitted = now_ns();

		if (bench->mode_bridge) {
			uint64_t bridge_start;

			if (queue_push(&bench->queue, &slot) != 0) {
				die("queue stopped unexpectedly");
			}

			pthread_mutex_lock(&slot.mutex);
			while (slot.completed_seq != slot.seq) {
				pthread_cond_wait(&slot.cond, &slot.mutex);
			}
			completed = now_ns();
			bridge_start = slot.bridge_start_ns;
			pthread_mutex_unlock(&slot.mutex);
			stats_add(worker, completed - submitted, bridge_start >= submitted ? bridge_start - submitted : 0);
		} else {
			jlong result = (*env)->CallStaticLongMethod(env,
			                                             bench->bootstrap.target_class,
			                                             bench->bootstrap.consume_method,
			                                             (jlong) slot.seq);
			check_jni(env, "CallStaticLongMethod");
			completed = now_ns();
			stats_add(worker, completed - submitted, 0);
			slot.result = (uint64_t) result;
		}
	}

	pthread_cond_destroy(&slot.cond);
	pthread_mutex_destroy(&slot.mutex);
	if (attached > 0) {
		(*bench->bootstrap.jvm)->DetachCurrentThread(bench->bootstrap.jvm);
	}
	return NULL;
}

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s MODE WORKERS ITERS [BRIDGE_THREADS]\n", prog);
	fprintf(stderr, "  MODE: direct | bridge\n");
}

int main(int argc, char **argv)
{
	struct bench_context bench;
	pthread_t bootstrap_thread;
	struct bridge_context bridge_ctx;
	uint64_t start;
	uint64_t end;
	uint64_t total_ops;
	uint64_t elapsed;
	uint64_t total_latency = 0;
	uint64_t total_queue_wait = 0;
	uint64_t min_latency = 0;
	uint64_t max_latency = 0;

	if (argc < 4 || argc > 5) {
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	memset(&bench, 0, sizeof(bench));
	bench.worker_count = atoi(argv[2]);
	bench.iterations = strtoull(argv[3], NULL, 10);
	bench.mode_bridge = strcmp(argv[1], "bridge") == 0;
	bench.bridge_count = bench.mode_bridge ? (argc == 5 ? atoi(argv[4]) : 1) : 0;

	if (bench.worker_count <= 0 || bench.worker_count > 256 ||
	    bench.iterations == 0 || bench.bridge_count < 0 || bench.bridge_count > 256) {
		die("invalid benchmark arguments");
	}
	if (!bench.mode_bridge && strcmp(argv[1], "direct") != 0) {
		die("MODE must be direct or bridge");
	}

	pthread_mutex_init(&bench.bootstrap.mutex, NULL);
	pthread_cond_init(&bench.bootstrap.cond, NULL);
	queue_init(&bench.queue);

	if (pthread_create(&bootstrap_thread, NULL, bootstrap_main, &bench.bootstrap) != 0) {
		die("pthread_create bootstrap failed");
	}

	pthread_mutex_lock(&bench.bootstrap.mutex);
	while (!bench.bootstrap.ready) {
		pthread_cond_wait(&bench.bootstrap.cond, &bench.bootstrap.mutex);
	}
	if (bench.bootstrap.failed) {
		pthread_mutex_unlock(&bench.bootstrap.mutex);
		die("JVM bootstrap failed");
	}
	pthread_mutex_unlock(&bench.bootstrap.mutex);

	bench.workers = calloc((size_t) bench.worker_count, sizeof(*bench.workers));
	bench.worker_threads = calloc((size_t) bench.worker_count, sizeof(*bench.worker_threads));
	bench.bridge_threads = bench.bridge_count > 0 ? calloc((size_t) bench.bridge_count, sizeof(*bench.bridge_threads)) : NULL;
	if (bench.workers == NULL || bench.worker_threads == NULL ||
	    (bench.bridge_count > 0 && bench.bridge_threads == NULL)) {
		die("allocation failed");
	}

	bridge_ctx.bench = &bench;
	for (int i = 0; i < bench.bridge_count; i++) {
		if (pthread_create(&bench.bridge_threads[i], NULL, bridge_main, &bridge_ctx) != 0) {
			die("pthread_create bridge failed");
		}
	}

	start = now_ns();
	for (int i = 0; i < bench.worker_count; i++) {
		bench.workers[i].bench = &bench;
		bench.workers[i].index = i;
		bench.workers[i].iterations = bench.iterations;
		if (pthread_create(&bench.worker_threads[i], NULL, worker_main, &bench.workers[i]) != 0) {
			die("pthread_create worker failed");
		}
	}

	for (int i = 0; i < bench.worker_count; i++) {
		pthread_join(bench.worker_threads[i], NULL);
	}
	end = now_ns();

	queue_stop(&bench.queue);
	for (int i = 0; i < bench.bridge_count; i++) {
		pthread_join(bench.bridge_threads[i], NULL);
	}

	pthread_mutex_lock(&bench.bootstrap.mutex);
	bench.bootstrap.shutdown = 1;
	pthread_cond_signal(&bench.bootstrap.cond);
	pthread_mutex_unlock(&bench.bootstrap.mutex);
	pthread_join(bootstrap_thread, NULL);

	total_ops = (uint64_t) bench.worker_count * bench.iterations;
	elapsed = end - start;
	for (int i = 0; i < bench.worker_count; i++) {
		total_latency += bench.workers[i].total_ns;
		total_queue_wait += bench.workers[i].queue_wait_ns;
		if (min_latency == 0 || (bench.workers[i].min_ns != 0 && bench.workers[i].min_ns < min_latency)) {
			min_latency = bench.workers[i].min_ns;
		}
		if (bench.workers[i].max_ns > max_latency) {
			max_latency = bench.workers[i].max_ns;
		}
	}

	printf("mode=%s workers=%d bridge_threads=%d iterations=%llu ops=%llu elapsed_ns=%llu throughput_ops_s=%.3f avg_op_ns=%.3f min_op_ns=%llu max_op_ns=%llu avg_queue_wait_ns=%.3f\n",
	       argv[1],
	       bench.worker_count,
	       bench.bridge_count,
	       (unsigned long long) bench.iterations,
	       (unsigned long long) total_ops,
	       (unsigned long long) elapsed,
	       (double) total_ops * 1e9 / (double) elapsed,
	       (double) total_latency / (double) total_ops,
	       (unsigned long long) min_latency,
	       (unsigned long long) max_latency,
	       (double) total_queue_wait / (double) total_ops);

	free(bench.bridge_threads);
	free(bench.worker_threads);
	free(bench.workers);
	return EXIT_SUCCESS;
}
