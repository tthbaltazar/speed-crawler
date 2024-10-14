#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct path_queue
{
	struct path_queue *next;
	char *path;
};

static struct path_queue *queue_head = NULL;
static struct path_queue *queue_tail = NULL;
static int queue_length = 0;

static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t finish_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

/*
	waits until every single enqueued item was acknowledged as processed
*/
static void wait_for_all_processed()
{
	pthread_mutex_lock(&queue_lock);
	for(;;)
	{
		if (queue_length == 0)
		{
			break;
		}

		pthread_cond_wait(&finish_cond, &queue_lock);
	}
	pthread_mutex_unlock(&queue_lock);
}

/*
	argument is a path as a string,
	should be allocated with malloc,
	ownership is taken by the function and will free it
*/
static void enqueue(char *path)
{
	assert(path != NULL);

	struct path_queue *to_add = malloc(sizeof(*to_add));
	assert(to_add != NULL);
	memset(to_add, 0, sizeof(*to_add));
	to_add->path = path;

	pthread_mutex_lock(&queue_lock);
	queue_length += 1;

	if (queue_tail != NULL)
	{
		queue_tail->next = to_add;
	}

	queue_tail = to_add;

	if (queue_head == NULL)
	{
		queue_head = to_add;
	}

	pthread_cond_signal(&queue_cond);
	pthread_mutex_unlock(&queue_lock);
}

/*
	returns a path as a string,
	is allocated with malloc,
	caller should free it after they don't need it
*/
static char* dequeue()
{
	struct path_queue *item = NULL;

	pthread_mutex_lock(&queue_lock);

	/* get head (lol)*/
	for(;;)
	{
		item = queue_head;
		if (item != NULL)
		{
			break;
		}
		pthread_cond_wait(&queue_cond, &queue_lock);
	}

	/* if tail points to the item unset the tail */
	if (item == queue_tail)
	{
		queue_tail = NULL;
	}

	/* move head */
	queue_head = item->next;

	/* neither head or tail should point to item */
	if (queue_head == item || queue_tail == item)
	{
		fprintf(stderr, "dequeue: What a Terrible Failure: head or tail points to item after dequeue %p %p %p\n", item, queue_head, queue_tail);
		assert(0);
	}

	pthread_mutex_unlock(&queue_lock);

	char *path = item->path;
	free(item);

	return path;
}

/*
	signals that an item was processed
*/
static void queue_processed()
{
	pthread_mutex_lock(&queue_lock);

	queue_length -= 1;

	if (queue_length == 0)
	{
		pthread_cond_signal(&finish_cond);
	}

	pthread_mutex_unlock(&queue_lock);
}

/*
	return base + "/" + name + \0
*/
static char* join_path(char *base, char *name)
{
	int total_length = strlen(base) + 1 + strlen(name) + 1;

	char *full_path = malloc(total_length);
	assert(full_path != NULL);
	memset(full_path, 0, total_length);

	strcat(full_path, base);
	strcat(full_path, "/");
	strcat(full_path, name);

	return full_path;
}

static void process_directory(char *path)
{
	assert(path != NULL);

	printf("%s\n", path);

	DIR *dir = opendir(path);
	for(;;)
	{
		if (dir == NULL)
		{
			fprintf(stderr, "failed to opendir: %s\n", path);
			break;
		}

		struct dirent *ent = readdir(dir);
		if (ent == NULL)
		{
			closedir(dir);
			break;
		}

		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
		{
			continue;
		}

		if (ent->d_type == DT_DIR)
		{
			char *full_path = join_path(path, ent->d_name);
			enqueue(full_path);
		}
	}
}

static void* crawl(void* _arg)
{
	for(;;)
	{
		if (0 && queue_length == 0)
		{
			fprintf(stderr, "crawl: exiting\n");
			break;
		}

		char *path = dequeue();
		if (path == NULL)
		{
			continue;
		}

		process_directory(path);

		free(path);

		queue_processed();
	}
	return NULL;
}

int main(int argc, char **argv)
{	
	enqueue(strdup("/usr"));

	for (int i = 0; i < 64; i++)
	{
		pthread_t t;
		pthread_create(&t, NULL, crawl, NULL);
	}

	wait_for_all_processed();

	return 0;
}
