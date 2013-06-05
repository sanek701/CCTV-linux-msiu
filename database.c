#include "cameras.h"
#include <libpq-fe.h>
#include <libconfig.h>

extern char *store_dir;

static config_t config;
static PGconn *conn;
static pthread_mutex_t db_lock;

static const char *read_db_setting(char* path) {
  const char *str;
  if(config_lookup_string(&config, path, &str) != CONFIG_TRUE) {
    fprintf(stderr, "Missing setting: %s\n", path);
    config_destroy(&config);
    exit(EXIT_FAILURE);
  }
  return str;
}

static PGresult *exec_query(char* query) {
  pthread_mutex_lock(&db_lock);
  PGresult *result = PQexec(conn, query);
  pthread_mutex_unlock(&db_lock);
  return result;
}

static void print_camera(struct camera *cam) {
  fprintf(stderr, "Camera %d <url: %s, analize_frames: %d, threshold: %d, motion_delay: %d>\n",
    cam->id, cam->url, cam->analize_frames, cam->threshold, cam->motion_delay);
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
    fprintf(stderr, "database OK\n");
  }

  if(pthread_mutex_init(&db_lock, NULL) < 0) {
    fprintf(stderr, "pthread_mutex_init failed\n");
    exit(EXIT_FAILURE);
  }
}

void close_pg_conn() {
  PQfinish(conn);
  pthread_mutex_destroy(&db_lock);
  free(store_dir);
}

struct camera* select_cameras(int *ncams) {
  int i;
  int n_cameras;
  struct camera* cameras;
  PGresult *result = PQexec(conn, "SELECT id, url, code, analize_frames, threshold, motion_delay FROM cameras ORDER BY id;");
  if(PQresultStatus(result) != PGRES_TUPLES_OK) {
    fprintf(stderr, "No cameras found: %s\n", PQresultErrorMessage(result));
    PQclear(result);
  }

  n_cameras = PQntuples(result);
  cameras = (struct camera*)malloc(n_cameras * sizeof(struct camera));
  memset(cameras, 0, n_cameras * sizeof(struct camera));

  for(i=0; i < n_cameras; i++) {
    char *id = PQgetvalue(result, i, 0);
    char *url = PQgetvalue(result, i, 1);
    char *name = PQgetvalue(result, i, 2);
    char *analize_frames = PQgetvalue(result, i, 3);
    char *threshold = PQgetvalue(result, i, 4);
    char *motion_delay = PQgetvalue(result, i, 5);

    cameras[i].url = (char*)malloc(strlen(url)+1);
    cameras[i].name = (char*)malloc(strlen(name)+1);
    strcpy(cameras[i].url, url);
    strcpy(cameras[i].name, name);
    sscanf(id, "%d", &cameras[i].id);
    sscanf(analize_frames, "%d", &cameras[i].analize_frames);
    sscanf(threshold, "%d", &cameras[i].threshold);
    sscanf(motion_delay, "%d", &cameras[i].motion_delay);

    cameras[i].last_screenshot = 0;
    cameras[i].active = 1;
    cameras[i].cam_consumers_list = NULL;

    print_camera(&cameras[i]);
  }
  PQclear(result);

  *ncams = n_cameras;
  return cameras;
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
