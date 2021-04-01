/*
 * Goodvibes Radio Player
 *
 * Copyright (C) 2015-2021 Arnaud Rebillout
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <gtk/gtk.h>

/*
 * Version Information
 */

const gchar *
gtk_get_runtime_version_string(void)
{
	static gchar *version_string;

	if (version_string == NULL) {
		version_string = g_strdup_printf("GTK %u.%u.%u",
		                                 gtk_get_major_version(),
		                                 gtk_get_minor_version(),
		                                 gtk_get_micro_version());
	}

	return version_string;
}

const gchar *
gtk_get_compile_version_string(void)
{
	static gchar *version_string;

	if (version_string == NULL) {
		version_string = g_strdup_printf("GTK %u.%u.%u",
		                                 GTK_MAJOR_VERSION,
		                                 GTK_MINOR_VERSION,
		                                 GTK_MICRO_VERSION);
	}

	return version_string;
}
