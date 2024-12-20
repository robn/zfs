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
#include <sys/debug.h>
#include "libzutil.h"

/* column header. name, accumulated width and style (render properties) */
typedef struct {
	char		*tc_name;
	size_t		tc_width;
} ztable_col_t;

/*
 * cell. an actual unit of data in the table. carries the stringified data,
 * and its width.
 */
typedef struct {
	char 		*tcl_data;
	size_t		tcl_width;
} ztable_cell_t;

/* row. list of cells */
typedef struct {
	size_t		tr_acells;
	size_t		tr_ncells;
	ztable_cell_t	*tr_cells;
} ztable_row_t;

/* the table proper */
struct ztable {
	size_t		t_acols;
	size_t		t_ncols;
	ztable_col_t	*t_cols;

	size_t		t_arows;
	size_t		t_nrows;
	ztable_row_t	*t_rows;
};

/* ========== */

ztable_t *
ztable_create(void)
{
	ztable_t *t = calloc(1, sizeof (ztable_t));
	return (t);
}

void
ztable_add_column(ztable_t *t, const char *name,
    const ztable_colspec_t *colspec)
{
	(void) colspec;

	/* can't add columns once the first row is started */
	ASSERT0(t->t_nrows);

	if (t->t_ncols == t->t_acols) {
		t->t_acols = t->t_acols ? t->t_acols << 1 : 8;
		t->t_cols = realloc(t->t_cols,
		    t->t_acols * sizeof (ztable_col_t));
	}
	ztable_col_t *col = &t->t_cols[t->t_ncols++];

	col->tc_name = strdup(name);
	col->tc_width = strlen(name);
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

	cell->tcl_data = strdup((const char *)data);
	cell->tcl_width = strlen(cell->tcl_data);

	ztable_col_t *col = &t->t_cols[row->tr_ncells-1];
	col->tc_width = MAX(col->tc_width, cell->tcl_width);
}

void
ztable_add_row(ztable_t *t, const void *data[])
{
	if (t->t_nrows > 0)
		/* forcibly close previous row */
		t->t_rows[t->t_nrows-1].tr_ncells = t->t_ncols;

	for (size_t i = 0; i < t->t_ncols; i++)
		ztable_add_cell(t, data[i]);
}

void
ztable_print(ztable_t *t)
{
	for(size_t i = 0; i < t->t_ncols; i++)
		printf("%-*s", (int)t->t_cols[i].tc_width+2,
		    t->t_cols[i].tc_name);
	printf("\n");

	for (size_t ri = 0; ri < t->t_nrows; ri++) {
		ztable_row_t *row = &t->t_rows[ri];
		for (size_t i = 0; i < row->tr_ncells; i++) {
			ztable_cell_t *cell = &row->tr_cells[i];
			printf("%-*s", (int)t->t_cols[i].tc_width+2,
			    cell->tcl_data);
		}
		printf("\n");
	}
}

void
ztable_destroy(ztable_t *t)
{
	for(size_t i = 0; i < t->t_ncols; i++)
		free(t->t_cols[i].tc_name);
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
