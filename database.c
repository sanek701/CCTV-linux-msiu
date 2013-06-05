#include "cameras.h"
#include <sys/types.h>
#include <sys/stat.h>

static config_t config;
PGconn *conn;
pthread_mutex_t db_lock;

char *store_dir;

static const char *read_db_setting(char* path) {
  const char *str;
  if(config_lookup_string(&config, path, &str) != CONFIG_TRUE) {
    fprintf(stderr, "Missing setting: %s\n", path);
    config_destroy(&config);
    exit(EXIT_FAILURE);
  }
  return str;
}

PGresult *exec_query(char* query) {
  pthread_mutex_lock(&db_lock);
  PGresult *result = PQexec(conn, query);
  pthread_mutex_unlock(&db_lock);
  return result;
}

void init_pg_conn(const char *conf_file) {
  if(config_read_file(&config, conf_file) != CONFIG_TRUE) {
    fprintf(stderr, "%s:%d %s\n", config_error_file(&config), config_error_line(&config), config_error_text(&config));
    config_destroy(&config);
    exit(EXIT_FAILURE);
  }

  conn = PQsetdbLogin(
    read_db_setting("db_host"),
    read_db_setting("db_port"),
    NULL, NULL,
    read_db_setting("db_name"),
    read_db_setting("db_login"),
    read_db_setting("db_password"));

  const char *store_dir_tmp = read_db_setting("store_dir");
  store_dir = (char*)malloc(strlen(store_dir_tmp)+1);
  strcpy(store_dir, store_dir_tmp);

  if(PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
    config_destroy(&config);
    exit(EXIT_FAILURE);
  } else {
    printf("database OK\n");
  }

  if(pthread_mutex_init(&db_lock, NULL) < 0) {
    printf("pthread_mutex_init failed\n");
    exit(EXIT_FAILURE);
  }
}

void close_pg_conn() {
  PQfinish(conn);
  pthread_mutex_destroy(&db_lock);
  free(store_dir);
}

void create_event(int cam_id, time_t raw_started_at, time_t raw_finished_at) {
  char query[256];
  snprintf(query, sizeof(query), "INSERT INTO events(camera_id, started_at, finished_at) VALUES (%u, to_timestamp(%u) at time zone 'UTC', to_timestamp(%u) at time zone 'UTC');",
    cam_id, (unsigned int)raw_started_at, (unsigned int)raw_finished_at);
  printf("%s\n", query);
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Insertion failed(event): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }
  PQclear(result);
}

void create_videofile(struct camera *cam, char *filepath) {
  char query[256], date[11], datetime[15];
  cam->file_started_at = time(NULL);
  snprintf(query, sizeof(query), "INSERT INTO videofiles(camera_id, started_at) VALUES (%u, to_timestamp(%u) at time zone 'UTC') RETURNING (id);",
    cam->id, (unsigned int)cam->file_started_at);
  printf("%s\n", query);
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Insertion failed(videofile): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }
  sscanf(PQgetvalue(result, 0, PQfnumber(result, "id")), "%d", &cam->file_id);
  PQclear(result);

  strftime(date, 11, "%Y%m%d", localtime(&cam->file_started_at));
  strftime(datetime, 15, "%Y%m%d%H%M%S", localtime(&cam->file_started_at));

  snprintf(filepath, 1024, "%s/%s", store_dir, cam->name);
  mkdir(filepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filepath, 1024, "%s/%s/%s", store_dir, cam->name, date);
  mkdir(filepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filepath, 1024, "%s/%s/%s/%s.h264", store_dir, cam->name, 
date, datetime);
}

void update_videofile(struct camera *cam) {
  char query[256];
  snprintf(query, sizeof(query), "UPDATE videofiles SET finished_at = to_timestamp(%u) at time zone 'UTC' WHERE id = %d;",
    (unsigned int)time(NULL), cam->file_id);
  printf("%s\n", query);
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Update failed(videofile): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }
  PQclear(result);
}
