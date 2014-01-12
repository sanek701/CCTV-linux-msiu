#include "camera.h"
#include <unistd.h>
#include <strings.h>
#include <libpq-fe.h>
#include <libconfig.h>
#include <sys/stat.h>
#include <sys/types.h>

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

static PGresult* exec_query(char* query) {
  pthread_mutex_lock(&db_lock);
  fprintf(stderr, "%s\n", query);
  PGresult *result = PQexec(conn, query);
  pthread_mutex_unlock(&db_lock);
  return result;
}

static void print_camera(struct camera *cam) {
  fprintf(stderr, "Camera %d <url: %s, analize_frames: %d, threshold: %d, motion_delay: %d>\n",
    cam->id, cam->url, cam->analize_frames, cam->threshold, cam->motion_delay);
}

void db_init_pg_conn(const char *conf_file) {
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

void db_close_pg_conn() {
  PQfinish(conn);
  pthread_mutex_destroy(&db_lock);
  free(store_dir);
}

struct camera* db_select_cameras(int *ncams) {
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

    if(pthread_mutex_init(&cameras[i].consumers_lock, NULL) < 0) {
      fprintf(stderr, "pthread_mutex_init failed\n");
      exit(EXIT_FAILURE);
    }

    print_camera(&cameras[i]);
  }
  PQclear(result);

  *ncams = n_cameras;
  return cameras;
}

void db_create_event(int cam_id, time_t raw_started_at, time_t raw_finished_at) {
  char query[256];
  snprintf(query, sizeof(query),
    "INSERT INTO events(camera_id, started_at, finished_at) VALUES (%u, to_timestamp(%ld) at time zone 'UTC', to_timestamp(%ld) at time zone 'UTC');",
    cam_id, raw_started_at, raw_finished_at);
  
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Insertion failed(event): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }

  PQclear(result);
}

void db_create_videofile(struct camera *cam, char *filepath) {
  char query[256], date[11], datetime[15];
  cam->file_started_at = time(NULL);
  snprintf(query, sizeof(query), "INSERT INTO videofiles(camera_id, started_at) VALUES (%u, to_timestamp(%ld) at time zone 'UTC') RETURNING (id);",
    cam->id, cam->file_started_at);
  
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Insertion failed(videofile): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }

  sscanf(PQgetvalue(result, 0, PQfnumber(result, "id")), "%d", &cam->file_id);
  PQclear(result);

  strftime(date, sizeof(date), "%Y%m%d", localtime(&cam->file_started_at));
  strftime(datetime, sizeof(datetime), "%Y%m%d%H%M%S", localtime(&cam->file_started_at));

  snprintf(filepath, 1024, "%s/%s", store_dir, cam->name);
  mkdir(filepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filepath, 1024, "%s/%s/%s", store_dir, cam->name, date);
  mkdir(filepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filepath, 1024, "%s/%s/%s/%s.h264", store_dir, cam->name, date, datetime);
}

void db_update_videofile(struct camera *cam) {
  char query[256];
  snprintf(query, sizeof(query), "UPDATE videofiles SET finished_at = to_timestamp(%ld) at time zone 'UTC' WHERE id = %d;",
    time(NULL), cam->file_id);
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Update failed(videofile): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }
  PQclear(result);
}

static char *_mp4 = "mp4";
static char *_h264 = "h264";
static char* get_extension(int ext) {
  if(ext == MP4_FILE)
    return _mp4;
  else
    return _h264;
}

static char* get_cam_code(int cam_id) {
  char query[256], *cam_code, *ret;
  snprintf(query, sizeof(query), "SELECT code FROM cameras where id = %d;", cam_id);
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Selection failed(cameras): %s\n", PQresultErrorMessage(result));
    return NULL;
  }
  if(PQntuples(result) == 1) {
    cam_code = PQgetvalue(result, 0, 0);
    ret = (char *)malloc(strlen(cam_code) + 1);
    strcpy(ret, cam_code);
    PQclear(result);
    return ret;
  } else {
    PQclear(result);
    fprintf(stderr, "No camera found\n");
    return NULL;
  }
}

static int mk_file_name(int cam_id, time_t *start_ts, char *mp4, char *fname) {
  char date_part[32];
  int extension = H264_FILE;
  char *cam_code = get_cam_code(cam_id);
  if(cam_code == NULL)
    return -1;

  if(strcmp(mp4, "t") == 0)
    extension = MP4_FILE;
  strftime(date_part, sizeof(date_part), "%Y%m%d/%Y%m%d%H%M%S", localtime(start_ts));
  snprintf(fname, 1024, "%s/%s/%s.%s", store_dir, cam_code, date_part, get_extension(extension));
  free(cam_code);
  return extension;
}

int db_find_video_file(int cam_id, char *fname, time_t *timestamp) {
  char query[256];
  time_t start_ts;
  int found, extension;

  snprintf(query, sizeof(query),
    "SELECT date_part('epoch', started_at at time zone 'UTC'), mp4 FROM videofiles WHERE camera_id = %d AND started_at <= to_timestamp(%ld) at time zone 'UTC' AND (finished_at >= to_timestamp(%ld) at time zone 'UTC' OR finished_at IS NULL);",
    cam_id, *timestamp, *timestamp);

  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Selection failed(videofile): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }

  found = PQntuples(result);
  if(found == 1) {
    char *ts  = PQgetvalue(result, 0, 0);
    char *mp4 = PQgetvalue(result, 0, 1);
    
    sscanf(ts, "%ld", &start_ts);

    *timestamp -= start_ts;

    extension = mk_file_name(cam_id, &start_ts, mp4, fname);
    if(extension < 0) {
      PQclear(result);
      return -1;
    }

    fprintf(stderr, "Found file: %s at %d\n", fname, (int)*timestamp);
  } else {
    extension = -1;
    fprintf(stderr, "Files found: %d\n", found);
  }

  PQclear(result);

  return extension;
}

void db_update_mp4_file_flag(int file_id) {
  char query[256];
  snprintf(query, sizeof(query), "UPDATE videofiles SET mp4 = true WHERE id = %d;", file_id);
  PGresult *result = exec_query(query);
  if(PQresultStatus(result) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Update failed(videofile): %s\n", PQresultErrorMessage(result));
    exit(EXIT_FAILURE);
  }
  PQclear(result);
}

int db_unlink_oldest_file() {
  int video_id, camera_id;
  time_t start_ts;
  char query[256], fname[1024];

  strcpy(query, "SELECT id, camera_id, date_part('epoch', started_at at time zone 'UTC'), mp4 FROM videofiles WHERE finished_at IS NOT NULL AND deleted_at is NULL ORDER BY started_at DESC LIMIT 1;");
  PGresult *result = exec_query(query);
  if(PQntuples(result) == 1) {
    char *id     = PQgetvalue(result, 0, 0);
    char *cam_id = PQgetvalue(result, 0, 1);
    char *ts     = PQgetvalue(result, 0, 2);
    char *mp4    = PQgetvalue(result, 0, 3);

    sscanf(id,     "%d", &video_id);
    sscanf(cam_id, "%d", &camera_id);
    sscanf(ts,    "%ld", &start_ts);

    if(mk_file_name(camera_id, &start_ts, mp4, fname) < 0) {
      PQclear(result);
      return -1;
    }

    PQclear(result);

    snprintf(query, sizeof(query), "UPDATE videofiles SET deleted_at = NOW() at time zone 'UTC' WHERE id = %d;", video_id);
    PGresult *result = exec_query(query);
    if(PQresultStatus(result) == PGRES_COMMAND_OK) {
      fprintf(stderr, "Deleting: %s\n", fname);
      unlink(fname);
      return 1;
    } else {
      fprintf(stderr, "Update failed(videofile): %s\n", PQresultErrorMessage(result));
      exit(EXIT_FAILURE);
    }
  }

  PQclear(result);
  return 0;
}
