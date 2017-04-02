/*
 * Goodvibes Radio Player
 *
 * Copyright (C) 2015-2017 Arnaud Rebillout
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <glib.h>

#include "framework/log.h"
#include "framework/gv-file-helpers.h"

/*
 * File I/O
 */

gboolean
gv_file_read_sync(const gchar *path, gchar **text, GError **err)
{
	g_assert(text);

	return g_file_get_contents(path, text, NULL, err);
}

gboolean
gv_file_write_sync(const gchar *path, const gchar *text, GError **err)
{
	return g_file_set_contents(path, text, -1, err);
}

/*
 * Pathes
 */

static gchar *
gv_get_current_dir(const gchar *subdir)
{
	gchar *dir;
	gchar *current_dir;

	current_dir = g_get_current_dir();
	dir = g_build_filename(current_dir, subdir, NULL);
	g_free(current_dir);

	return dir;
}

const gchar *
gv_get_current_config_dir(void)
{
	static gchar *dir;

	if (dir == NULL)
		dir = gv_get_current_dir("config");

	return dir;
}

const gchar *
gv_get_current_data_dir(void)
{
	static gchar *dir;

	if (dir == NULL)
		dir = gv_get_current_dir("data");

	return dir;
}

const gchar *
gv_get_user_data_dir(void)
{
	static gchar *dir;

	if (dir == NULL) {
		const gchar *user_dir;
		gboolean created;

		user_dir = g_get_user_data_dir();
		dir = g_build_filename(user_dir, PACKAGE_NAME, NULL);

		created = g_mkdir_with_parents(dir, S_IRWXU);
		if (created != 0)
			WARNING("Failed to make user data dir '%s': %s",
			        dir, strerror(errno));
	}

	return dir;
}

const gchar *
gv_get_user_config_dir(void)
{
	static gchar *dir;

	if (dir == NULL) {
		const gchar *user_dir;
		gboolean created;

		user_dir = g_get_user_config_dir();
		dir = g_build_filename(user_dir, PACKAGE_NAME, NULL);

		created = g_mkdir_with_parents(dir, S_IRWXU);
		if (created != 0)
			WARNING("Failed to make user config dir '%s': %s",
			        dir, strerror(errno));
	}

	return dir;
}

const gchar *const *
gv_get_system_config_dirs(void)
{
	static gchar **dirs;

	if (dirs == NULL) {
		const gchar *const *system_dirs;
		guint i, n_dirs;

		system_dirs = g_get_system_config_dirs();
		for (i = 0, n_dirs = 0; system_dirs[i] != NULL; i++)
			n_dirs++;

		dirs = g_malloc0_n(n_dirs + 1, sizeof(gchar *));
		for (i = 0; system_dirs[i] != NULL; i++)
			dirs[i] = g_build_filename(system_dirs[i], PACKAGE_NAME, NULL);
	}

	return (const gchar * const *) dirs;
}

const gchar *const *
gv_get_system_data_dirs(void)
{
	static gchar **dirs;

	if (dirs == NULL) {
		const gchar *const *system_dirs;
		guint i, n_dirs;

		system_dirs = g_get_system_data_dirs();
		for (i = 0, n_dirs = 0; system_dirs[i] != NULL; i++)
			n_dirs++;

		dirs = g_malloc0_n(n_dirs + 1, sizeof(gchar *));
		for (i = 0; system_dirs[i] != NULL; i++)
			dirs[i] = g_build_filename(system_dirs[i], PACKAGE_NAME, NULL);
	}

	return (const gchar * const *) dirs;
}

GSList *
gv_get_path_list(GvDirType dir_type, const gchar *filename)
{
	GSList *list = NULL;
	gchar *path;

	if (dir_type & GV_DIR_CURRENT_CONFIG) {
		path = g_build_filename(gv_get_current_config_dir(), filename, NULL);
		list = g_slist_append(list, path);
	}

	if (dir_type & GV_DIR_CURRENT_DATA) {
		path = g_build_filename(gv_get_current_data_dir(), filename, NULL);
		list = g_slist_append(list, path);
	}

	if (dir_type & GV_DIR_USER_CONFIG) {
		path = g_build_filename(gv_get_user_config_dir(), filename, NULL);
		list = g_slist_append(list, path);
	}

	if (dir_type & GV_DIR_USER_DATA) {
		path = g_build_filename(gv_get_user_data_dir(), filename, NULL);
		list = g_slist_append(list, path);
	}

	if (dir_type & GV_DIR_SYSTEM_CONFIG) {
		const gchar *const *system_dirs;
		guint i;

		system_dirs = gv_get_system_config_dirs();

		for (i = 0; system_dirs[i] != NULL; i++) {
			path = g_build_filename(system_dirs[i], filename, NULL);
			list = g_slist_append(list, path);
		}
	}

	if (dir_type & GV_DIR_SYSTEM_DATA) {
		const gchar *const *system_dirs;
		guint i;

		system_dirs = gv_get_system_data_dirs();

		for (i = 0; system_dirs[i] != NULL; i++) {
			path = g_build_filename(system_dirs[i], filename, NULL);
			list = g_slist_append(list, path);
		}
	}

	return list;
}

GSList *
gv_get_existing_path_list(GvDirType dir_type, const gchar *filename)
{
	GSList *pathes, *item;

	pathes = gv_get_path_list(dir_type, filename);
	item = pathes;

	while (item) {
		GSList *next_item;
		gchar *file;

		next_item = item->next;

		file = (gchar *) item->data;
		if (!g_file_test(file, G_FILE_TEST_EXISTS)) {
			pathes = g_slist_delete_link(pathes, item);
			g_free(file);
		}

		item = next_item;
	}

	return pathes;
}

gchar *
gv_get_first_existing_path(GvDirType dir_type, const gchar *filename)
{
	GSList *pathes, *item;
	gchar *path = NULL;

	pathes = gv_get_path_list(dir_type, filename);

	for (item = pathes; item; item = item->next) {
		const gchar *file;

		file = (const gchar *) item->data;
		if (g_file_test(file, G_FILE_TEST_EXISTS)) {
			path = g_strdup(file);
			break;
		}
	}

	g_slist_free_full(pathes, g_free);

	return path;
}
