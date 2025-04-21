#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <curl/curl.h>
#include <string.h>
#include <unistd.h>

#define MAX_QUEUE 8 // Size of bounded buffer

typedef struct {
    int thread_id;
    char url[2048];
    long start;
    long end;
} ThreadData;

typedef struct {
    int id;
    long size;
    char *data;
} Chunk;

Chunk *queue[MAX_QUEUE];
int head = 0, tail = 0, count = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

int total_chunks = 0;
int chunks_written = 0;
FILE *output_file;

size_t write_memory_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb;
    Chunk *chunk = (Chunk *)userdata;

    char *new_data = realloc(chunk->data, chunk->size + total_size);
    if (!new_data) {
        fprintf(stderr, "Realloc failed!\n");
        return 0;
    }

    chunk->data = new_data;
    memcpy(chunk->data + chunk->size, ptr, total_size);
    chunk->size += total_size;

    return total_size;
}

void enqueue(Chunk *chunk) {
    pthread_mutex_lock(&mutex);
    while (count == MAX_QUEUE)
        pthread_cond_wait(&not_full, &mutex);

    queue[tail] = chunk;
    tail = (tail + 1) % MAX_QUEUE;
    count++;

    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mutex);
}

Chunk *dequeue() {
    pthread_mutex_lock(&mutex);
    while (count == 0)
        pthread_cond_wait(&not_empty, &mutex);

    Chunk *chunk = queue[head];
    head = (head + 1) % MAX_QUEUE;
    count--;

    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&mutex);
    return chunk;
}

void *download_chunk(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Thread %d: CURL init failed\n", data->thread_id);
        return NULL;
    }

    char range[64];
    snprintf(range, sizeof(range), "%ld-%ld", data->start, data->end);

    Chunk *chunk = malloc(sizeof(Chunk));
    chunk->id = data->thread_id;
    chunk->size = 0;
    chunk->data = NULL;

    curl_easy_setopt(curl, CURLOPT_URL, data->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, chunk);
    curl_easy_setopt(curl, CURLOPT_RANGE, range);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Thread %d: Download failed: %s\n",
                data->thread_id, curl_easy_strerror(res));
        if (chunk->data) free(chunk->data);
        free(chunk);
    } else {
        fprintf(stderr, "Thread %d downloaded chunk [%ld - %ld], size: %ld\n",
                data->thread_id, data->start, data->end, chunk->size);
        enqueue(chunk);
    }

    curl_easy_cleanup(curl);
    return NULL;
}

void *writer_thread(void *arg) {
    Chunk **received_chunks = calloc(total_chunks, sizeof(Chunk *));
    
    while (chunks_written < total_chunks) {
        Chunk *chunk = dequeue();
        received_chunks[chunk->id] = chunk;

        while (received_chunks[chunks_written]) {
            Chunk *c = received_chunks[chunks_written];
            fwrite(c->data, 1, c->size, output_file);
            free(c->data);
            free(c);
            received_chunks[chunks_written] = NULL;
            chunks_written++;
        }
    }

    free(received_chunks);
    return NULL;
}

double get_file_size(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    double content_length = -1;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
    }
    curl_easy_cleanup(curl);
    return content_length;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <URL> <num_threads> <output_file>\n", argv[0]);
        return 1;
    }

    char *url = argv[1];
    int num_threads = atoi(argv[2]);
    char *output_filename = argv[3];

    curl_global_init(CURL_GLOBAL_ALL);

    double file_size = get_file_size(url);
    if (file_size <= 0) {
        fprintf(stderr, "Failed to get file size.\n");
        return 1;
    }

    long chunk_size = file_size / num_threads;
    total_chunks = num_threads;

    output_file = fopen(output_filename, "wb");
    if (!output_file) {
        perror("fopen");
        return 1;
    }

    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];
    pthread_t writer;

    pthread_create(&writer, NULL, writer_thread, NULL);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        strcpy(thread_data[i].url, url);
        thread_data[i].start = i * chunk_size;
        thread_data[i].end = (i == num_threads - 1) ? (long)file_size - 1 : (i + 1) * chunk_size - 1;
        pthread_create(&threads[i], NULL, download_chunk, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_join(writer, NULL);
    fclose(output_file);
    curl_global_cleanup();

    printf("✅ Download complete: %s\n", output_filename);
    return 0;
}

