#include <sys/types.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <sqlite3.h>
#include <fts.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkg_util.h"


int
pkg_create_repo(const char *path, void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS	*fts;
	FTSENT	*ent;

	struct stat st;
	struct pkg *pkg, **deps;
	struct pkg_file **files;
	char *ext = NULL;
	sqlite3 *sqlite;
	sqlite3_stmt *stmt_files;
	sqlite3_stmt *stmt_deps;
	sqlite3_stmt *stmt_pkg;

	int i;

	char *repopath[2], repodb[MAXPATHLEN];

	const char initsql[] = ""
		"CREATE TABLE packages ("
			"origin TEXT PRIMARY KEY,"
			"name TEXT,"
			"version TEXT,"
			"comment TEXT,"
			"desc TEXT,"
			"arch TEXT,"
			"osversion TEXT,"
			"maintainer TEXT,"
			"www TEXT,"
			"pkg_format_version INTEGER,"
			"size INTEGER"
		");"
		"CREATE TABLE deps ("
			"origin TEXT,"
			"name TEXT,"
			"version TEXT,"
			"package_id TEXT REFERENCES packages(origin),"
			"PRIMARY KEY (package_id, origin)"
		");"
		"CREATE INDEX deps_origin ON deps (origin);"
		"CREATE INDEX deps_package ON deps (package_id);"
		"CREATE TABLE files ("
			"path TEXT,"
			"size INTEGER,"
			"package_id TEXT REFERENCES packages(origin),"
			"PRIMARY KEY (package_id, path)"
		");"
		"CREATE INDEX files_packages ON files (package_id);"
		"CREATE INDEX files_path ON files (path);"
		;


	if (!is_dir(path))
		return (EPKG_FATAL);

	repopath[0] = (char *)path;
	repopath[1] = NULL;

	snprintf(repodb, MAXPATHLEN, "%s/repo.db", path);

	if (stat(repodb, &st) != -1)
		if (unlink(repodb) != 0)
			return (EPKG_FATAL);

	if (sqlite3_open(repodb, &sqlite) != SQLITE_OK)
		return (EPKG_FATAL);


	if (sqlite3_exec(sqlite, initsql, NULL, NULL, NULL) != SQLITE_OK)
		return (EPKG_FATAL);

	sqlite3_exec(sqlite, "BEGIN TRANSACTION;", NULL, NULL, NULL);

	sqlite3_prepare(sqlite, "INSERT INTO packages (origin, name, version, comment, desc, arch, osversion, maintainer, www, pkg_format_version, size) "
			"values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);",
			-1, &stmt_pkg, NULL);
	sqlite3_prepare(sqlite, "INSERT INTO deps (origin, name, version, package_id) "
			"values (?1, ?2, ?3, ?4);",
			-1, &stmt_deps, NULL);
	sqlite3_prepare(sqlite, "INSERT INTO files (path, size, package_id) "
			"values (?1, ?2, ?3);",
			-1, &stmt_files, NULL);

	fts = fts_open(repopath, FTS_PHYSICAL, NULL);

	while ((ent = fts_read(fts)) != NULL) {
		/* skip everything that is not a file */
		if (ent->fts_info != FTS_F)
			continue;

		ext = strrchr(ent->fts_name, '.');
		if (strcmp(ext, ".tgz") != 0 &&
				strcmp(ext, ".tbz") != 0 &&
				strcmp(ext, ".txz") != 0 &&
				strcmp(ext, ".tar") != 0)
			continue;

		if (pkg_open(ent->fts_path, &pkg, 0) != EPKG_OK)
			continue;

		if (progress != NULL)
			progress(pkg, data);
		sqlite3_bind_text(stmt_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 2, pkg_get(pkg, PKG_NAME), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 3, pkg_get(pkg, PKG_VERSION), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 4, pkg_get(pkg, PKG_COMMENT), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 5, pkg_get(pkg, PKG_DESC), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 6, pkg_get(pkg, PKG_ARCH), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 7, pkg_get(pkg, PKG_OSVERSION), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 8, pkg_get(pkg, PKG_MAINTAINER), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt_pkg, 9, pkg_get(pkg, PKG_WWW), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt_pkg, 11, ent->fts_statp->st_size);

		sqlite3_step(stmt_pkg);
		sqlite3_reset(stmt_pkg);

		if ((deps = pkg_deps(pkg)) != NULL) {
			for (i = 0; deps[i] != NULL; i++) {
				sqlite3_bind_text(stmt_deps, 1, pkg_get(deps[i], PKG_ORIGIN), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt_deps, 2, pkg_get(deps[i], PKG_NAME), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt_deps, 3, pkg_get(deps[i], PKG_VERSION), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt_deps, 4, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_TRANSIENT);

				sqlite3_step(stmt_deps);
				sqlite3_reset(stmt_deps);
			}
		}

		if ((files = pkg_files(pkg)) != NULL) {
			for (i = 0; files[i] != NULL; i++) {
				sqlite3_bind_text(stmt_files, 1, pkg_file_path(files[i]), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(stmt_files, 2, pkg_file_size(files[i]));
				sqlite3_bind_text(stmt_files, 3, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_TRANSIENT);

				sqlite3_step(stmt_files);
				sqlite3_reset(stmt_files);
			}
		}

		pkg_free(pkg);
	}

	sqlite3_finalize(stmt_pkg);
	sqlite3_finalize(stmt_files);
	sqlite3_finalize(stmt_deps);

	sqlite3_exec(sqlite, "COMMIT;", NULL, NULL, NULL);

	sqlite3_close(sqlite);

	if (progress != NULL)
		progress(NULL, data);

	return (EPKG_OK);
}
