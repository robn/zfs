// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 * Copyright (c) 2026, TrueNAS.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/debug.h>
#include "libzutil.h"

/* ========== */

/* style: layout and formatting controls */

typedef struct {
	bool		s_format;
	bool		s_color;
	bool		s_header;
	bool		s_headline;
	bool		s_midline;
	bool		s_footline;
	bool		s_pad_cells;
	size_t		s_edge_gap;
	bool		s_edge_border;
	size_t		s_cell_gap;
	bool		s_cell_border;
	const char	**s_chars;
} ztable_stylespec_t;

static const ztable_colspec_t default_colspec = {
	.cs_type = ZT_TYPE_STRING,
	.cs_align = ZT_ALIGN_LEFT,
	.cs_effect = ZT_EFFECT_DEFAULT,
	.cs_header_effect = ZT_EFFECT_DEFAULT,
	.cs_color = ZT_COLOR_DEFAULT,
	.cs_header_color = ZT_COLOR_DEFAULT,
};

typedef enum {
    BC_PAD,
    BC_HORIZ,
    BC_VERT,
    BC_CORNER_TOP_LEFT,
    BC_CORNER_TOP_RIGHT,
    BC_CORNER_BOTTOM_LEFT,
    BC_CORNER_BOTTOM_RIGHT,
    BC_JOIN_RIGHT,
    BC_JOIN_LEFT,
    BC_JOIN_BOTTOM,
    BC_JOIN_TOP,
    BC_JOIN_CROSS,
} ztable_charspec_t;

static const char *ascii_chars[] =
    { " ", "-", "|", "/", "\\", "\\", "/", "+", "+", "+", "+", "+" };
static const char *box_heavy_chars[] =
    { " ", "━", "┃", "┏", "┓", "┗", "┛", "┣", "┫", "┳", "┻", "╋" };
static const char *box_double_chars[] =
    { " ", "═", "║", "╔", "╗", "╚", "╝", "╠", "╣", "╦", "╩", "╬" };
static const char *scripted_chars[] =
    { "\t", "", "", "", "", "", "", "", "", "", "", "" };

static const char *ansi_reset			= "\x1b[0m";

static const char *ansi_effect_bold		= "\x1b[1m";
static const char *ansi_effect_dim		= "\x1b[2m";
static const char *ansi_effect_italic		= "\x1b[3m";
static const char *ansi_effect_underline	= "\x1b[4m";
static const char *ansi_effect_strikethrough	= "\x1b[9m";

static const char *ansi_color_black		= "\x1b[0;30m";
static const char *ansi_color_red		= "\x1b[0;31m";
static const char *ansi_color_green		= "\x1b[0;32m";
static const char *ansi_color_yellow		= "\x1b[0;33m";
static const char *ansi_color_blue		= "\x1b[0;34m";
static const char *ansi_color_magenta		= "\x1b[0;35m";
static const char *ansi_color_cyan		= "\x1b[0;36m";
static const char *ansi_color_gray		= "\x1b[0;37m";

typedef struct {
	ztable_charspec_t cs_pad;
	ztable_charspec_t cs_edge_border_left;
	ztable_charspec_t cs_edge_border_right;
	ztable_charspec_t cs_edge_gap;
	ztable_charspec_t cs_cell_border;
	ztable_charspec_t cs_cell_gap;
} ztable_cellcharspec_t;

static const ztable_cellcharspec_t headlinespec = {
    BC_HORIZ,
    BC_CORNER_TOP_LEFT, BC_CORNER_TOP_RIGHT, BC_HORIZ,
    BC_JOIN_BOTTOM, BC_HORIZ,
};
static const ztable_cellcharspec_t midlinespec = {
    BC_HORIZ,
    BC_JOIN_RIGHT, BC_JOIN_LEFT, BC_HORIZ,
    BC_JOIN_CROSS, BC_HORIZ,
};
static const ztable_cellcharspec_t footlinespec = {
    BC_HORIZ,
    BC_CORNER_BOTTOM_LEFT, BC_CORNER_BOTTOM_RIGHT, BC_HORIZ,
    BC_JOIN_TOP, BC_HORIZ,
};
static const ztable_cellcharspec_t headingspec = {
    BC_PAD,
    BC_VERT, BC_VERT, BC_PAD,
    BC_VERT, BC_PAD,
};
static const ztable_cellcharspec_t dataspec = {
    BC_PAD,
    BC_VERT, BC_VERT, BC_PAD,
    BC_VERT, BC_PAD,
};

static const ztable_stylespec_t classic_style = {
	.s_format = true,
	.s_color = true,
	.s_header = true,
	.s_headline = false,
	.s_midline = false,
	.s_footline = false,
	.s_pad_cells = true,
	.s_edge_gap = 0,
	.s_edge_border = false,
	.s_cell_gap = 2,
	.s_cell_border = false,
	.s_chars = ascii_chars,
};

static const ztable_stylespec_t simple_style = {
	.s_format = true,
	.s_color = true,
	.s_header = true,
	.s_headline = true,
	.s_midline = true,
	.s_footline = true,
	.s_pad_cells = true,
	.s_edge_gap = 0,
	.s_edge_border = false,
	.s_cell_gap = 1,
	.s_cell_border = true,
	.s_chars = ascii_chars,
};

static const ztable_stylespec_t box_style = {
	.s_format = true,
	.s_color = true,
	.s_header = true,
	.s_headline = true,
	.s_midline = true,
	.s_footline = true,
	.s_pad_cells = true,
	.s_edge_gap = 1,
	.s_edge_border = true,
	.s_cell_gap = 1,
	.s_cell_border = true,
	.s_chars = box_heavy_chars,
};

static const ztable_stylespec_t double_style = {
	.s_format = true,
	.s_color = true,
	.s_header = true,
	.s_headline = true,
	.s_midline = true,
	.s_footline = true,
	.s_pad_cells = true,
	.s_edge_gap = 1,
	.s_edge_border = true,
	.s_cell_gap = 1,
	.s_cell_border = true,
	.s_chars = box_double_chars,
};

static const ztable_stylespec_t scripted_style = {
	.s_format = false,
	.s_color = false,
	.s_header = false,
	.s_headline = false,
	.s_midline = false,
	.s_footline = false,
	.s_pad_cells = false,
	.s_edge_gap = 0,
	.s_edge_border = false,
	.s_cell_gap = 1,
	.s_cell_border = false,
	.s_chars = scripted_chars,
};

static const ztable_stylespec_t *
default_style(void)
{
	const char *env_style = getenv("ZFS_TABLE_STYLE");
	if (env_style == NULL)
		return (&box_style);	/* XXX temp for dev */
	if (strcmp(env_style, "classic") == 0)
		return (&classic_style);
	if (strcmp(env_style, "simple") == 0)
		return (&simple_style);
	if (strcmp(env_style, "box") == 0)
		return (&box_style);
	if (strcmp(env_style, "double") == 0)
		return (&double_style);
	return (&classic_style);
}

static bool
color_effects_available(void)
{
	if (!isatty(STDOUT_FILENO))
		return (false);

	const char *env = getenv("NO_COLOR");
	if (env)
		return (false);

	return (true);
}

/* ========== */

/*
 * cell. an actual unit of data in the table. carries the stringified data,
 * and its width.
 */
typedef struct {
	char 		*tcl_data;
	size_t		tcl_width;
} ztable_cell_t;

/* column header. a cell for display, and accumulated width */
typedef struct {
	ztable_colspec_t	tc_spec;
	size_t			tc_max_width;

	ztable_cell_t		tc_cell;
} ztable_col_t;

/* row. list of cells */
typedef struct {
	size_t		tr_acells;
	size_t		tr_ncells;
	ztable_cell_t	*tr_cells;
} ztable_row_t;

/* the table proper */
struct ztable {
	ztable_stylespec_t	t_style;

	size_t			t_acols;
	size_t			t_ncols;
	ztable_col_t		*t_cols;

	size_t			t_arows;
	size_t			t_nrows;
	ztable_row_t		*t_rows;
};

/* ========== */

ztable_t *
ztable_create(ztable_style_t style)
{
	ztable_t *t = calloc(1, sizeof (ztable_t));

	switch (style) {
	case ZT_STYLE_CLASSIC:
		t->t_style = classic_style;
		break;
	case ZT_STYLE_SIMPLE:
		t->t_style = simple_style;
		break;
	case ZT_STYLE_BOX:
		t->t_style = box_style;
		break;
	case ZT_STYLE_DOUBLE:
		t->t_style = double_style;
		break;

	case ZT_STYLE_SCRIPTED:
		t->t_style = scripted_style;
		break;

	default:
		t->t_style = *default_style();
		break;
	}

	if (t->t_style.s_color && !color_effects_available())
		t->t_style.s_color = false;

	return (t);
}

void
ztable_add_column(ztable_t *t, const char *name,
    const ztable_colspec_t *colspec)
{
	/* can't add columns once the first row is started */
	ASSERT0(t->t_nrows);

	if (t->t_ncols == t->t_acols) {
		t->t_acols = t->t_acols ? t->t_acols << 1 : 8;
		t->t_cols = realloc(t->t_cols,
		    t->t_acols * sizeof (ztable_col_t));
	}
	ztable_col_t *col = &t->t_cols[t->t_ncols++];

	col->tc_spec = colspec == NULL ? default_colspec : *colspec;

	col->tc_cell.tcl_data = strdup(name);
	col->tc_cell.tcl_width = strlen(name);

	if (t->t_style.s_header)
		col->tc_max_width = col->tc_cell.tcl_width;

	if (!t->t_style.s_format)
		col->tc_spec.cs_format = ZT_FORMAT_RAW;
}

void
ztable_add_cell(ztable_t *t, const void *data)
{
	ztable_row_t *row;
	if (t->t_nrows == 0 ||
	    t->t_rows[t->t_nrows-1].tr_ncells == t->t_ncols) {
		/* No rows, or the last row is full. Start a new row. */
		if (t->t_nrows == t->t_arows) {
			t->t_arows = t->t_arows ? t->t_arows << 1 : 8;
			t->t_rows = realloc(t->t_rows,
			    t->t_arows * sizeof (ztable_row_t));
		}
		row = &t->t_rows[t->t_nrows++];
		memset(row, 0, sizeof (ztable_row_t));
	} else {
		/* continuing existing row */
		row = &t->t_rows[t->t_nrows-1];
	}

	if (row->tr_ncells == row->tr_acells) {
		row->tr_acells = row->tr_acells ? row->tr_acells << 1 : 8;
		row->tr_cells = realloc(row->tr_cells,
		    row->tr_acells * sizeof (ztable_cell_t));
	}
	ztable_cell_t *cell = &row->tr_cells[row->tr_ncells++];

	ztable_col_t *col = &t->t_cols[row->tr_ncells-1];

	if (data == NULL)
		cell->tcl_data = strdup("-");
	else {
		switch (col->tc_spec.cs_type) {
		case ZT_TYPE_STRING:
			cell->tcl_data = strdup((const char *)data);
			break;
		case ZT_TYPE_UINT64: {
			uint64_t v = *(const uint64_t *)data;
			char buf[64];
			switch (col->tc_spec.cs_format) {
			case ZT_FORMAT_NUMBER:
				zfs_nicenum(v, buf, sizeof (buf));
				break;
			case ZT_FORMAT_BYTES:
				zfs_nicebytes(v, buf, sizeof (buf));
				break;
			case ZT_FORMAT_TIME: {
				time_t t = (time_t)v;
				struct tm tm;
				if (localtime_r(&t, &tm) == NULL ||
				    strftime(buf, sizeof (buf), "%c", &tm) == 0)
					goto fallback;
				break;
			}
			default:
fallback:
				snprintf(buf, sizeof (buf), "%" PRIu64, v);
				break;
			}
			cell->tcl_data = strdup(buf);
			break;
		}
		}
	}

	cell->tcl_width = strlen(cell->tcl_data);

	col->tc_max_width = MAX(col->tc_max_width, cell->tcl_width);
}

void
ztable_add_row(ztable_t *t, ...)
{
	if (t->t_nrows > 0)
		/* forcibly close previous row */
		t->t_rows[t->t_nrows-1].tr_ncells = t->t_ncols;

	va_list ap;
	va_start(ap, t);

	for (size_t i = 0; i < t->t_ncols; i++) {
		const void *data = va_arg(ap, const void *);
		ztable_add_cell(t, data);
	}
}

void
ztable_destroy(ztable_t *t)
{
	for(size_t i = 0; i < t->t_ncols; i++)
		free(t->t_cols[i].tc_cell.tcl_data);
	free(t->t_cols);

	for (size_t ri = 0; ri < t->t_nrows; ri++) {
		ztable_row_t *row = &t->t_rows[ri];
		for (size_t i = 0; i < row->tr_ncells; i++)
			free(row->tr_cells[i].tcl_data);
		free(row->tr_cells);
	}
	free(t->t_rows);

	free(t);
}

/* ========== */

typedef enum {
	ZT_CELL_HEADER,
	ZT_CELL_DATA,
	ZT_CELL_DECORATION,
} ztable_celltype_t;

static size_t
ztable_format_cell(ztable_t *t, ztable_cell_t *cell,
    ztable_celltype_t celltype, size_t colidx,
    const ztable_cellcharspec_t *cs, char *buf, size_t bufsz)
{
	const ztable_stylespec_t *ss = &t->t_style;
	ztable_col_t *col = &t->t_cols[colidx];

	size_t bp = 0;

	if (colidx == 0) {
		if (ss->s_edge_border)
			/* edge border (left) */
			bp += strlcpy(&buf[bp],
			    ss->s_chars[cs->cs_edge_border_left], bufsz-bp);
		/* edge gap (left) */
		for (size_t p = 0; p < ss->s_edge_gap; p++)
			bp += strlcpy(&buf[bp],
			    ss->s_chars[cs->cs_edge_gap], bufsz-bp);
	} else {
		/* cell gap (left) */
		for (size_t p = 0; p < ss->s_cell_gap; p++)
			bp += strlcpy(&buf[bp],
			    ss->s_chars[cs->cs_cell_gap], bufsz-bp);
		if (ss->s_cell_border) {
			/* cell border */
			bp += strlcpy(&buf[bp],
			    ss->s_chars[cs->cs_cell_border], bufsz-bp);
			/* cell gap (right) */
			for (size_t p = 0; p < ss->s_cell_gap; p++)
				bp += strlcpy(&buf[bp],
				    ss->s_chars[cs->cs_cell_gap], bufsz-bp);
		}
	}

	size_t lpad = 0, rpad = 0;
	if (ss->s_pad_cells) {
		size_t pad = col->tc_max_width - (cell ? cell->tcl_width : 0);
		switch (col->tc_spec.cs_align) {
		case ZT_ALIGN_LEFT:
			rpad = pad;
			break;
		case ZT_ALIGN_RIGHT:
			lpad = pad;
			rpad = 0;
			break;
		case ZT_ALIGN_CENTER:
			lpad = pad/2;
			rpad = pad/2 + (pad & 1);
			break;
		}
	}

	/* padding (left) */
	for (size_t p = 0; p < lpad; p++)
		bp += strlcpy(&buf[bp], ss->s_chars[cs->cs_pad], bufsz-bp);

	/* content */
	if (cell) {
		/* color */
		ztable_colspec_color_t color =
		    !ss->s_color ? ZT_COLOR_DEFAULT :
		    celltype == ZT_CELL_HEADER ? col->tc_spec.cs_header_color :
		    celltype == ZT_CELL_DATA ? col->tc_spec.cs_color :
		    ZT_COLOR_DEFAULT;

		switch (color) {
		case ZT_COLOR_BLACK:
			bp += strlcpy(&buf[bp], ansi_color_black, bufsz-bp);
			break;
		case ZT_COLOR_RED:
			bp += strlcpy(&buf[bp], ansi_color_red, bufsz-bp);
			break;
		case ZT_COLOR_GREEN:
			bp += strlcpy(&buf[bp], ansi_color_green, bufsz-bp);
			break;
		case ZT_COLOR_YELLOW:
			bp += strlcpy(&buf[bp], ansi_color_yellow, bufsz-bp);
			break;
		case ZT_COLOR_BLUE:
			bp += strlcpy(&buf[bp], ansi_color_blue, bufsz-bp);
			break;
		case ZT_COLOR_MAGENTA:
			bp += strlcpy(&buf[bp], ansi_color_magenta, bufsz-bp);
			break;
		case ZT_COLOR_CYAN:
			bp += strlcpy(&buf[bp], ansi_color_cyan, bufsz-bp);
			break;
		case ZT_COLOR_GRAY:
			bp += strlcpy(&buf[bp], ansi_color_gray, bufsz-bp);
			break;
		default:
			break;
		}
		/* effect */
		ztable_colspec_effect_t effect =
		    !ss->s_color ? ZT_EFFECT_DEFAULT :
		    celltype == ZT_CELL_HEADER ? col->tc_spec.cs_header_effect :
		    celltype == ZT_CELL_DATA ? col->tc_spec.cs_effect :
		    ZT_EFFECT_DEFAULT;

		switch (effect) {
		case ZT_EFFECT_BOLD:
			bp += strlcpy(&buf[bp], ansi_effect_bold, bufsz-bp);
			break;
		case ZT_EFFECT_DIM:
			bp += strlcpy(&buf[bp], ansi_effect_dim, bufsz-bp);
			break;
		case ZT_EFFECT_ITALIC:
			bp += strlcpy(&buf[bp], ansi_effect_italic, bufsz-bp);
			break;
		case ZT_EFFECT_UNDERLINE:
			bp += strlcpy(&buf[bp],
			    ansi_effect_underline, bufsz-bp);
			break;
		case ZT_EFFECT_STRIKETHROUGH:
			bp += strlcpy(&buf[bp],
			    ansi_effect_strikethrough, bufsz-bp);
			break;
		default:
			break;
		}

		/* data */
		bp += strlcpy(&buf[bp], cell->tcl_data, bufsz-bp);

		/* effect/color reset */
		if (effect != ZT_EFFECT_DEFAULT || color != ZT_COLOR_DEFAULT)
			bp += strlcpy(&buf[bp], ansi_reset, bufsz-bp);
	}

	/* padding (right) */
	for (size_t p = 0; p < rpad; p++)
		bp += strlcpy(&buf[bp], ss->s_chars[cs->cs_pad], bufsz-bp);

	if (colidx == t->t_ncols-1) {
		/* edge gap (right) */
		for (size_t p = 0; p < ss->s_edge_gap; p++)
			bp += strlcpy(&buf[bp],
			    ss->s_chars[cs->cs_edge_gap], bufsz-bp);
		if (ss->s_edge_border)
			/* edge border (right) */
			bp += strlcpy(&buf[bp],
			    ss->s_chars[cs->cs_edge_border_right], bufsz-bp);
	}

	return (bp);
}

void
ztable_print(ztable_t *t, FILE *fp)
{
	if (t->t_ncols == 0)
		return;

	if (fp == NULL)
		fp = stdout;

	const ztable_stylespec_t *ss = &t->t_style;

	/* content width */
	size_t width = 0;
	for (size_t i = 0; i < t->t_ncols; i++)
		width += t->t_cols[i].tc_max_width;

	/* edge gap */
	width += ss->s_edge_gap * 2;

	/* edge border */
	if (ss->s_edge_border)
		/* +1 for border glyph each side */
		width += 2;

	/* cell gap */
	width += (t->t_ncols-1) * ss->s_cell_gap;

	/* cell border */
	if (ss->s_cell_border)
		/* +1 for border glyph, + cell gap on the other side */
		width += (t->t_ncols-1) * (ss->s_cell_gap + 1);

	char buf[1024];
	size_t bufsz = sizeof (buf);
	size_t bp = 0;

	/*
	 * head border line. if header is disabled, but midline is enabled,
	 * draw the headline in place of the midline, since its a better
	 * style for the top of the table
	 */
	if ((ss->s_header && ss->s_headline) ||
	    (!ss->s_header && ss->s_midline))
	if (ss->s_headline) {
		bp = 0;
		for (size_t i = 0; i < t->t_ncols; i++)
			bp += ztable_format_cell(t, NULL, ZT_CELL_DECORATION,
			    i, &headlinespec, &buf[bp], bufsz-bp);
		fprintf(fp, "%s\n", buf);
	}

	if (ss->s_header) {
		/* header row */
		bp = 0;
		for (size_t i = 0; i < t->t_ncols; i++)
			bp += ztable_format_cell(t, &t->t_cols[i].tc_cell,
			    ZT_CELL_HEADER, i, &headingspec,
			    &buf[bp], bufsz-bp);
		fprintf(fp, "%s\n", buf);

		/* middle border line (separates header and data) */
		if (ss->s_midline) {
			bp = 0;
			for (size_t i = 0; i < t->t_ncols; i++)
				bp += ztable_format_cell(t, NULL,
				    ZT_CELL_DECORATION, i, &midlinespec,
				    &buf[bp], bufsz-bp);
			fprintf(fp, "%s\n", buf);
		}
	}

	/* data rows */
	for (size_t ri = 0; ri < t->t_nrows; ri++) {
		ztable_row_t *row = &t->t_rows[ri];
		bp = 0;
		for (size_t i = 0; i < row->tr_ncells; i++)
			bp += ztable_format_cell(t, &row->tr_cells[i],
			    ZT_CELL_DATA, i, &dataspec, &buf[bp], bufsz-bp);
		fprintf(fp, "%s\n", buf);
	}

	/* footer border line */
	if (ss->s_footline) {
		bp = 0;
		for (size_t i = 0; i < t->t_ncols; i++)
			bp += ztable_format_cell(t, NULL, ZT_CELL_DECORATION,
			    i, &footlinespec, &buf[bp], bufsz-bp);
		fprintf(fp, "%s\n", buf);
	}
}
