#include "lage_db.h"
#include <SPIFFS.h>
#include "sqlite3.h"

static sqlite3* db = nullptr;
static LageDbTimeFn g_timeFn = nullptr;

void lageDbSetTimeProvider(LageDbTimeFn fn) { g_timeFn = fn; }

static unsigned long lageDbNow() { return g_timeFn ? g_timeFn() : millis(); }

static bool execSimple(const char* sql) {
  char* errMsg = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK) {
    Serial.print("SQL-Fehler: ");
    Serial.println(errMsg);
    sqlite3_free(errMsg);
    return false;
  }
  return true;
}

bool lageDbBegin() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount fehlgeschlagen!");
    return false;
  }

  int rc = sqlite3_open("/spiffs/lage.db", &db);
  if (rc != SQLITE_OK) {
    Serial.println("DB konnte nicht geoeffnet werden!");
    return false;
  }

  // WICHTIG: kein PRIMARY KEY / UNIQUE hier -- loest auf SPIFFS zuverlaessig
  // "disk I/O error" aus (bekannter Bug der Sqlite3Esp32-Bibliothek, siehe
  // https://github.com/siara-cc/esp32_arduino_sqlite3_lib/issues/18 ).
  // Stattdessen nutzen wir SQLites eingebaute rowid als ID.
  execSimple(
    "CREATE TABLE IF NOT EXISTS lagemeldungen ("
    " kategorie TEXT,"
    " status TEXT,"
    " text TEXT,"
    " from_node TEXT,"
    " created_at INTEGER,"
    " updated_at INTEGER);"
  );
  execSimple(
    "CREATE TABLE IF NOT EXISTS lage_historie ("
    " lagemeldung_id INTEGER,"
    " changed_at INTEGER,"
    " alter_text TEXT,"
    " neuer_text TEXT,"
    " alter_status TEXT,"
    " neuer_status TEXT,"
    " from_node TEXT);"
  );
  // Issue #33: separate von lagemeldungen -- das hier ist die Betriebs-
  // historie der Saeule selbst (Aktivierungen, Sicherheits-Ablehnungen,
  // Fehler, ...), nicht der Inhalt der Lagemeldungen.
  execSimple(
    "CREATE TABLE IF NOT EXISTS ereignisse ("
    " zeit INTEGER,"
    " kategorie TEXT,"
    " text TEXT);"
  );
  return true;
}

int lageDbCreate(const String& kategorie, const String& status, const String& text, const String& fromNode) {
  sqlite3_stmt* stmt;
  const char* sql = "INSERT INTO lagemeldungen (kategorie, status, text, from_node, created_at, updated_at) "
                     "VALUES (?, ?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

  unsigned long now = lageDbNow();
  sqlite3_bind_text(stmt, 1, kategorie.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, fromNode.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, now);
  sqlite3_bind_int64(stmt, 6, now);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) return -1;

  return (int)sqlite3_last_insert_rowid(db); // funktioniert auch ohne PRIMARY KEY
}

bool lageDbUpdate(int id, const String& kategorie, const String& status, const String& text, const String& fromNode) {
  sqlite3_stmt* sel;
  const char* selSql = "SELECT text, status FROM lagemeldungen WHERE rowid = ?;";
  if (sqlite3_prepare_v2(db, selSql, -1, &sel, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int(sel, 1, id);

  if (sqlite3_step(sel) != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return false; // ID existiert nicht
  }
  String alterText = String((const char*)sqlite3_column_text(sel, 0));
  String alterStatus = String((const char*)sqlite3_column_text(sel, 1));
  sqlite3_finalize(sel);

  unsigned long now = lageDbNow();

  sqlite3_stmt* hist;
  const char* histSql = "INSERT INTO lage_historie "
    "(lagemeldung_id, changed_at, alter_text, neuer_text, alter_status, neuer_status, from_node) "
    "VALUES (?, ?, ?, ?, ?, ?, ?);";
  sqlite3_prepare_v2(db, histSql, -1, &hist, nullptr);
  sqlite3_bind_int(hist, 1, id);
  sqlite3_bind_int64(hist, 2, now);
  sqlite3_bind_text(hist, 3, alterText.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(hist, 4, text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(hist, 5, alterStatus.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(hist, 6, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(hist, 7, fromNode.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(hist);
  sqlite3_finalize(hist);

  sqlite3_stmt* upd;
  const char* updSql = "UPDATE lagemeldungen SET kategorie=?, status=?, text=?, from_node=?, updated_at=? WHERE rowid=?;";
  sqlite3_prepare_v2(db, updSql, -1, &upd, nullptr);
  sqlite3_bind_text(upd, 1, kategorie.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(upd, 2, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(upd, 3, text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(upd, 4, fromNode.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(upd, 5, now);
  sqlite3_bind_int(upd, 6, id);
  int rc = sqlite3_step(upd);
  sqlite3_finalize(upd);

  return rc == SQLITE_DONE;
}

static int printRowCallback(void* data, int argc, char** argv, char** colNames) {
  for (int i = 0; i < argc; i++) {
    Serial.print(colNames[i]);
    Serial.print("=");
    Serial.print(argv[i] ? argv[i] : "NULL");
    Serial.print("  ");
  }
  Serial.println();
  return 0;
}

void lageDbListSummary(const String& filterKategorie, const String& filterStatus) {
  String sql = "SELECT rowid AS id, kategorie, status, updated_at, text FROM lagemeldungen WHERE 1=1";
  if (filterKategorie.length() > 0) {
    sql += " AND kategorie = '" + filterKategorie + "'";
  }
  if (filterStatus.length() > 0) {
    sql += " AND status = '" + filterStatus + "'";
  }
  sql += " ORDER BY updated_at DESC;";

  Serial.println("--- Uebersicht Lagemeldungen ---");
  char* errMsg = nullptr;
  sqlite3_exec(db, sql.c_str(), printRowCallback, nullptr, &errMsg);
  if (errMsg) { Serial.println(errMsg); sqlite3_free(errMsg); }
}

int lageDbGetRecentSummaries(LageMeldungSummary* out, int maxCount) {
  const char* sql = "SELECT rowid, kategorie, status, text, updated_at "
                     "FROM lagemeldungen ORDER BY updated_at DESC LIMIT ?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
  sqlite3_bind_int(stmt, 1, maxCount);

  int count = 0;
  while (count < maxCount && sqlite3_step(stmt) == SQLITE_ROW) {
    out[count].id = sqlite3_column_int(stmt, 0);
    out[count].kategorie = String((const char*)sqlite3_column_text(stmt, 1));
    out[count].status = String((const char*)sqlite3_column_text(stmt, 2));
    out[count].text = String((const char*)sqlite3_column_text(stmt, 3));
    out[count].updatedAt = (unsigned long)sqlite3_column_int64(stmt, 4);
    count++;
  }
  sqlite3_finalize(stmt);
  return count;
}

void eventLog(const String& kategorie, const String& text) {
  Serial.printf("[EVENT] %s: %s\n", kategorie.c_str(), text.c_str());
  if (!db) return; // z.B. lageDbBegin() fehlgeschlagen -- nicht crashen, nur nicht persistieren
  sqlite3_stmt* stmt;
  const char* sql = "INSERT INTO ereignisse (zeit, kategorie, text) VALUES (?, ?, ?);";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
  sqlite3_bind_int64(stmt, 1, lageDbNow());
  sqlite3_bind_text(stmt, 2, kategorie.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

int eventLogGetRecent(EventLogEntry* out, int maxCount) {
  const char* sql = "SELECT rowid, zeit, kategorie, text FROM ereignisse ORDER BY zeit DESC LIMIT ?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
  sqlite3_bind_int(stmt, 1, maxCount);

  int count = 0;
  while (count < maxCount && sqlite3_step(stmt) == SQLITE_ROW) {
    out[count].id = sqlite3_column_int(stmt, 0);
    out[count].zeit = (unsigned long)sqlite3_column_int64(stmt, 1);
    out[count].kategorie = String((const char*)sqlite3_column_text(stmt, 2));
    out[count].text = String((const char*)sqlite3_column_text(stmt, 3));
    count++;
  }
  sqlite3_finalize(stmt);
  return count;
}

void lageDbShowDetail(int id) {
  Serial.print("--- Detail Lagemeldung ");
  Serial.print(id);
  Serial.println(" ---");

  String sql = "SELECT rowid AS id, kategorie, status, text, from_node, created_at, updated_at "
               "FROM lagemeldungen WHERE rowid = " + String(id) + ";";
  char* errMsg = nullptr;
  sqlite3_exec(db, sql.c_str(), printRowCallback, nullptr, &errMsg);
  if (errMsg) { Serial.println(errMsg); sqlite3_free(errMsg); }

  Serial.println("--- Historie ---");
  String histSql = "SELECT changed_at, alter_status, neuer_status, neuer_text, from_node "
                    "FROM lage_historie WHERE lagemeldung_id = " + String(id) + " ORDER BY changed_at ASC;";
  sqlite3_exec(db, histSql.c_str(), printRowCallback, nullptr, &errMsg);
  if (errMsg) { Serial.println(errMsg); sqlite3_free(errMsg); }
}