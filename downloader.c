#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>

#define MAX_QUEUE 8 //buffer 

typedef struct
 {
    int thread_id;
    char url[2048];
    long start;
    long end;
} ThreadData;

typedef struct
 {
    int id;
    long size; //=file/no of thread
    char *data; //wb
    long downloaded;
    long total;
} Chunk;


Chunk *queue[MAX_QUEUE];//buffer
int head = 0, tail = 0, count = 0; //in=tail   || out=head   ||count bufferSize ko track 

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;//sem_intit(&mutex,0,1)
pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER; 
pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;   

int total_chunks = 0;
int chunks_written = 0;
FILE *output_file = NULL;
GtkWidget *progress_bar;

size_t write_memory_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {  //curl
    size_t total_size = size * nmemb;
    Chunk *chunk = (Chunk *)userdata;

    char *new_data = realloc(chunk->data, chunk->size + total_size);
    if (!new_data) {
        fprintf(stderr, "Thread %d: realloc failed\n", chunk->id);
        return 0;
    }

    chunk->data = new_data;
    memcpy(chunk->data + chunk->size, ptr, total_size);
    chunk->size += total_size;
    chunk->downloaded += total_size;

    double progress = (double)chunk->downloaded / chunk->total * 100.0;
    printf("Thread %d downloading: %.2f%% (Bytes: %ld/%ld)\n",
           chunk->id, progress, chunk->downloaded, chunk->total);

    return total_size;
}

void enqueue(Chunk *chunk)
 {
    pthread_mutex_lock(&mutex);
    while (count == MAX_QUEUE)
        pthread_cond_wait(&not_full, &mutex);

    queue[tail] = chunk;
    tail = (tail + 1) % MAX_QUEUE;
    count++;
                                                                       //Producer 
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mutex);
}

Chunk *dequeue() 
{
    pthread_mutex_lock(&mutex);
    while (count == 0)
        pthread_cond_wait(&not_empty, &mutex);

    Chunk *chunk = queue[head];                                        //Consumer
    head = (head + 1) % MAX_QUEUE;
    count--;

    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&mutex);
    return chunk;
}

void *download_chunk(void *arg) {                 // ./downlaoder url 4 outfile -> GUI 
    ThreadData *data = (ThreadData *)arg;
    CURL *curl = curl_easy_init(); //initialize url 
    if (!curl) {
        fprintf(stderr, "Thread %d: CURL init failed\n", data->thread_id);
        return NULL;
    }

    char range[64];
    snprintf(range, sizeof(range), "%ld-%ld", data->start, data->end);//0-99 1,199,200-299

    Chunk *chunk = malloc(sizeof(Chunk));
    if (!chunk) {
        fprintf(stderr, "Thread %d: malloc failed\n", data->thread_id);//error handling
        curl_easy_cleanup(curl);
        return NULL;
    }
    chunk->id = data->thread_id;
    chunk->size = 0;
    chunk->data = NULL;
    chunk->downloaded = 0;
    chunk->total = data->end - data->start + 1; //99-0+1 //100

    curl_easy_setopt(curl, CURLOPT_URL, data->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);//curl call write_memory_callback
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, chunk);
    curl_easy_setopt(curl, CURLOPT_RANGE, range);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
     {
        fprintf(stderr, "Thread %d: Download failed: %s\n",
                data->thread_id, curl_easy_strerror(res));//error handling//
        if (chunk->data) free(chunk->data);
        free(chunk);
    } else {
        printf("Thread %d finished downloading chunk.\n", data->thread_id);
        enqueue(chunk);
    }

    curl_easy_cleanup(curl);
    return NULL;
}

void *writer_thread(void *arg)
 {
    Chunk **received_chunks = calloc(total_chunks, sizeof(Chunk *));
    if (!received_chunks) {
        fprintf(stderr, "Writer: calloc failed\n");//error Handling
        return NULL;
    }
    while (chunks_written < total_chunks) {
        Chunk *chunk = dequeue();//writing Into File
        if (!chunk) continue;
        received_chunks[chunk->id] = chunk;
        while (received_chunks[chunks_written]) {
            Chunk *c = received_chunks[chunks_written];
            if (fwrite(c->data, 1, c->size, output_file) != c->size) {
                fprintf(stderr, "Writer: fwrite failed\n");//error Handling
            }
            free(c->data);
            free(c);
            received_chunks[chunks_written] = NULL;
            chunks_written++;
            double progress = (double)chunks_written / total_chunks;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), progress);
            gtk_main_iteration_do(FALSE);
        }
    }
    free(received_chunks);
    return NULL;
}

double get_file_size(const char *url) 
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;//error Handling

    double content_length = -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);//setup for CuRL
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    CURLcode res = curl_easy_perform(curl);//basically performs the curl operation
    if (res == CURLE_OK) {
        res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);//fileSize maloom hojaiga
        if (res != CURLE_OK || content_length < 0) {
            fprintf(stderr, "Failed to get valid file size\n");
            content_length = -1;
        }
        
    } 
    else 
    {
        fprintf(stderr, "HEAD request failed: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return content_length;
}

void start_download(GtkWidget *widget, gpointer user_data) {
    GtkWidget *url_entry = g_object_get_data(G_OBJECT(widget), "url_entry");
    GtkWidget *threads_entry = g_object_get_data(G_OBJECT(widget), "num_threads");
    GtkWidget *output_entry = g_object_get_data(G_OBJECT(widget), "output_file");

    const char *url = gtk_entry_get_text(GTK_ENTRY(url_entry));//typecast
    const char *threads_text = gtk_entry_get_text(GTK_ENTRY(threads_entry));
    const char *output_filename = gtk_entry_get_text(GTK_ENTRY(output_entry));

    if (!url || !threads_text || !output_filename || strlen(url) == 0 || strlen(threads_text) == 0 || strlen(output_filename) == 0) {
        fprintf(stderr, "Error: All fields must be filled.\n");
        return;
    }

    int num_threads = atoi(threads_text);//./downloader url no of thead
    if (num_threads <= 0) {
        fprintf(stderr, "Error: Number of threads must be positive.\n");
        return;
    }

    double file_size = get_file_size(url);
    if (file_size <= 0) {
        fprintf(stderr, "Failed to get file size.\n");
        return;
    }

    long chunk_size = (long)file_size / num_threads;
    total_chunks = num_threads;

    output_file = fopen(output_filename, "wb");//write binary 
    if (!output_file) {
        perror("Error opening output file");
        return;
    }


    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];//stucture
    pthread_t writer;//writer produce 

    if (pthread_create(&writer, NULL, writer_thread, NULL) != 0) {//calls writerThread
        fprintf(stderr, "Error creating writer thread\n");
        fclose(output_file);
        return;
    }

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        strncpy(thread_data[i].url, url, sizeof(thread_data[i].url) - 1);
        thread_data[i].url[sizeof(thread_data[i].url) - 1] = '\0';
        thread_data[i].start = i * chunk_size;
        thread_data[i].end = (i == num_threads - 1) ? (long)file_size - 1 : (i + 1) * chunk_size - 1;
        if (pthread_create(&threads[i], NULL, download_chunk, &thread_data[i]) != 0) {
            fprintf(stderr, "Error creating thread %d\n", i);//Error Handling
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }//waits for all threads 

    pthread_join(writer, NULL);
    fclose(output_file);
    output_file = NULL;

    printf("\n✅ Download complete: %s\n", output_filename);
}

int main(int argc, char *argv[]) {

    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Download Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 350);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *url_entry = gtk_entry_new();              //   {"url_entry":url_entry}
    gtk_box_pack_start(GTK_BOX(vbox), url_entry, FALSE, FALSE, 0);
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), "Enter URL");

    GtkWidget *threads_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(vbox), threads_entry, FALSE, FALSE, 0);
    gtk_entry_set_placeholder_text(GTK_ENTRY(threads_entry), "Enter number of threads");

    GtkWidget *output_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(vbox), output_entry, FALSE, FALSE, 0);
    gtk_entry_set_placeholder_text(GTK_ENTRY(output_entry), "Enter output filename");

    progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), progress_bar, FALSE, FALSE, 0);

    GtkWidget *start_button = gtk_button_new_with_label("Start Download");
    gtk_box_pack_start(GTK_BOX(vbox), start_button, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(start_button), "url_entry", url_entry);
    g_object_set_data(G_OBJECT(start_button), "num_threads", threads_entry);
    g_object_set_data(G_OBJECT(start_button), "output_file", output_entry);

    g_signal_connect(start_button, "clicked", G_CALLBACK(start_download), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);//show all above made window
    gtk_main();//GUI Infinte Looop

    return 0;

}

