/* vim:set ts=8 sts=4 sw=4:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * diff.c: code for diff'ing two or three buffers.
 */

#include "vim.h"

#if defined(FEAT_DIFF) || defined(PROTO)

#define DB_COUNT 4	/* up to four buffers can be diff'ed */

/*
 * Each diffblock defines where a block of lines starts in each of the buffers
 * and how many lines it occupies in that buffer.  When the lines are missing
 * in the buffer the df_count[] is zero.  This is all counted in
 * buffer lines.
 * There is always at least one unchanged line in between the diffs.
 * Otherwise it would have been included in the diff above or below it.
 * df_lnum[] + df_count[] is the lnum below the change.  When in one buffer
 * lines have been inserted, in the other buffer df_lnum[] is the line below
 * the insertion and df_count[] is zero.  When appending lines at the end of
 * the buffer, df_lnum[] is one beyond the end!
 * This is using a linked list, because the number of differences is expected
 * to be reasonable small.  The list is sorted on lnum.
 */
typedef struct diffblock diff_T;
struct diffblock
{
    diff_T	*df_next;
    linenr_T	df_lnum[DB_COUNT];	/* line number in buffer */
    linenr_T	df_count[DB_COUNT];	/* nr of inserted/changed lines */
};

static diff_T	*first_diff = NULL;

static buf_T	*(diffbuf[DB_COUNT]);

static int	diff_invalid = TRUE;	/* list of diffs is outdated */

static int	diff_busy = FALSE;	/* ex_diffgetput() is busy */

/* Values set from 'diffopt'. */
static int	diff_context = 6;	/* context for folds */

/* flags obtained from the 'diffopt' option */
#define DIFF_FILLER	1	/* display filler lines */
#define DIFF_ICASE	2	/* ignore case */
#define DIFF_IWHITE	4	/* ignore change in white space */
static int	diff_flags = DIFF_FILLER;

static int diff_buf_idx __ARGS((buf_T *buf));
static void diff_check_unchanged __ARGS((diff_T *dp));
static void diff_clear __ARGS((void));
static int diff_equal_entry __ARGS((diff_T *dp, int idx1, int idx2));
static int diff_cmp __ARGS((char_u *s1, char_u *s2));
static void diff_read __ARGS((int idx_orig, int idx_new, char_u *fname));
static void diff_copy_entry __ARGS((diff_T *dprev, diff_T *dp, int idx_orig, int idx_new));
static diff_T *diff_alloc_new __ARGS((diff_T *dprev, diff_T *dp));

/*
 * Call this when a new buffer is being edited in the current window.  curbuf
 * must already have been set.
 * Marks the current buffer as being part of the diff and requiring updating.
 * This must be done before any autocmd, because a command the uses info
 * about the screen contents.
 */
    void
diff_new_buffer()
{
    if (curwin->w_p_diff)
	diff_buf_add(curbuf);
}

/*
 * Called when deleting or unloading a buffer: No longer make a diff with it.
 * Also called when 'diff' is reset in the last window showing a diff for a
 * buffer.
 */
    void
diff_buf_delete(buf)
    buf_T	*buf;
{
    int		i;

    i = diff_buf_idx(buf);
    if (i != DB_COUNT)
    {
	diffbuf[i] = NULL;
	diff_invalid = TRUE;
    }
}

/*
 * Add a buffer to make diffs for.
 */
    void
diff_buf_add(buf)
    buf_T	*buf;
{
    int		i;

    if (diff_buf_idx(buf) != DB_COUNT)
	return;		/* It's already there. */

    for (i = 0; i < DB_COUNT; ++i)
	if (diffbuf[i] == NULL)
	{
	    diffbuf[i] = buf;
	    diff_invalid = TRUE;
	    return;
	}

    EMSG2(_("Can not diff more than %ld buffers"), DB_COUNT);
}

/*
 * Find buffer "buf" in the list of diff buffers.
 * Return its index or DB_COUNT if not found.
 */
    static int
diff_buf_idx(buf)
    buf_T	*buf;
{
    int		idx;

    for (idx = 0; idx < DB_COUNT; ++idx)
	if (diffbuf[idx] == buf)
	    break;
    return idx;
}

/*
 * Mark the diff info as invalid, update it when info is requested.
 */
    void
diff_invalidate()
{
    if (curwin->w_p_diff)
	diff_invalid = TRUE;
}

/*
 * Called by mark_adjust(): update line numbers.
 * This attempts to update the changes as much as possible:
 * When inserting/deleting lines outside of existing change blocks, create a
 * new change block and update the line numbers in following blocks.
 * When inserting/deleting lines in existing change blocks, update them.
 */
    void
diff_mark_adjust(line1, line2, amount, amount_after)
    linenr_T	line1;
    linenr_T	line2;
    long	amount;
    long	amount_after;
{
    diff_T	*dp;
    diff_T	*dprev;
    diff_T	*dnext;
    int		idx;
    int		i;
    int		inserted, deleted;
    int		n, off;
    linenr_T	last;
    int		check_unchanged;

    /* Find the index for the current buffer. */
    idx = diff_buf_idx(curbuf);
    if (idx == DB_COUNT)
	return;		/* This buffer doesn't have diffs. */

    if (line2 == MAXLNUM)
    {
	/* mark_adjust(99, MAXLNUM, 9, 0): insert lines */
	inserted = amount;
	deleted = 0;
    }
    else if (amount_after > 0)
    {
	/* mark_adjust(99, 98, MAXLNUM, 9): a change that inserts lines*/
	inserted = amount_after;
	deleted = 0;
    }
    else
    {
	/* mark_adjust(98, 99, MAXLNUM, -2): delete lines */
	inserted = 0;
	deleted = -amount_after;
    }

    dprev = NULL;
    dp = first_diff;
    for (;;)
    {
	/* If the change is after the previous diff block and before the next
	 * diff block, thus not touching an existing change, create a new diff
	 * block.  Don't do this when ex_diffgetput() is busy. */
	if ((dp == NULL || dp->df_lnum[idx] - 1 > line2
		    || (line2 == MAXLNUM && dp->df_lnum[idx] > line1))
		&& (dprev == NULL
		    || dprev->df_lnum[idx] + dprev->df_count[idx] < line1)
		&& !diff_busy)
	{
	    dnext = diff_alloc_new(dprev, dp);
	    if (dnext == NULL)
		return;

	    dnext->df_lnum[idx] = line1;
	    dnext->df_count[idx] = inserted;
	    for (i = 0; i < DB_COUNT; ++i)
		if (diffbuf[i] != NULL && i != idx)
		{
		    if (dprev == NULL)
			dnext->df_lnum[i] = line1;
		    else
			dnext->df_lnum[i] = line1
			    + (dprev->df_lnum[i] + dprev->df_count[i])
			    - (dprev->df_lnum[idx] + dprev->df_count[idx]);
		    dnext->df_count[i] = deleted;
		}
	}

	/* if at end of the list, quit */
	if (dp == NULL)
	    break;

	/*
	 * Check for these situations:
	 *        1  2  3
	 *        1  2  3
	 * line1     2  3  4  5
	 *           2  3  4  5
	 *           2  3  4  5
	 * line2     2  3  4  5
	 *              3     5  6
	 *              3     5  6
	 */
	/* compute last line of this change */
	last = dp->df_lnum[idx] + dp->df_count[idx] - 1;

	/* 1. change completely above line1: nothing to do */
	if (last >= line1 - 1)
	{
	    /* 6. change below line2: only adjust for amount_after; also when
	     * "deleted" became zero when deleted all lines between two diffs */
	    if (dp->df_lnum[idx] - (deleted + inserted != 0) > line2)
	    {
		if (amount_after == 0)
		    break;	/* nothing left to change */
		dp->df_lnum[idx] += amount_after;
	    }
	    else
	    {
		check_unchanged = FALSE;

		/* 2. 3. 4. 5.: inserted/deleted lines touching this diff. */
		if (deleted > 0)
		{
		    if (dp->df_lnum[idx] >= line1)
		    {
			off = dp->df_lnum[idx] - line1;
			dp->df_lnum[idx] = line1;
			if (last <= line2)
			{
			    /* 4. delete all lines of diff */
			    if (dp->df_next != NULL
				    && dp->df_next->df_lnum[idx] - 1 <= line2)
			    {
				/* delete continues in next diff, only do
				 * lines until that one */
				n = dp->df_next->df_lnum[idx] - line1;
				deleted -= n;
				n -= dp->df_count[idx];
				line1 = dp->df_next->df_lnum[idx];
			    }
			    else
				n = deleted - dp->df_count[idx];
			    dp->df_count[idx] = 0;
			}
			else
			{
			    /* 5. delete lines at top of diff */
			    n = off;
			    dp->df_count[idx] -= line2 - dp->df_lnum[idx] + 1;
			    dp->df_lnum[idx] = line1;
			    check_unchanged = TRUE;
			}
		    }
		    else
		    {
			off = 0;
			if (last < line2)
			{
			    /* 2. delete at end of of diff */
			    dp->df_count[idx] -= last - line1 + 1;
			    if (dp->df_next != NULL
				    && dp->df_next->df_lnum[idx] - 1 <= line2)
			    {
				/* delete continues in next diff, only do
				 * lines until that one */
				n = dp->df_next->df_lnum[idx] - 1 - last;
				deleted -= dp->df_next->df_lnum[idx] - line1;
				line1 = dp->df_next->df_lnum[idx];
			    }
			    else
				n = line2 - last;
			    check_unchanged = TRUE;
			}
			else
			{
			    /* 3. delete lines inside the diff */
			    n = 0;
			    dp->df_count[idx] -= deleted;
			}
		    }

		    for (i = 0; i < DB_COUNT; ++i)
			if (diffbuf[i] != NULL && i != idx)
			{
			    dp->df_lnum[i] -= off;
			    dp->df_count[i] += n;
			}
		}
		else
		{
		    if (dp->df_lnum[idx] <= line1)
		    {
			/* inserted lines somewhere in this diff */
			dp->df_count[idx] += inserted;
			check_unchanged = TRUE;
		    }
		    else
			/* inserted lines somewhere above this diff */
			dp->df_lnum[idx] += inserted;
		}

		if (check_unchanged)
		    /* Check if inserted lines are equal, may reduce the
		     * size of the diff.  TODO: also check for equal lines
		     * in the middle and perhaps split the block. */
		    diff_check_unchanged(dp);
	    }
	}

	/* check if this block touches the previous one, may merge them. */
	if (dprev != NULL && dprev->df_lnum[idx] + dprev->df_count[idx]
							  == dp->df_lnum[idx])
	{
	    for (i = 0; i < DB_COUNT; ++i)
		if (diffbuf[i] != NULL)
		    dprev->df_count[i] += dp->df_count[i];
	    dprev->df_next = dp->df_next;
	    vim_free(dp);
	    dp = dprev->df_next;
	}
	else
	{
	    /* Advance to next entry. */
	    dprev = dp;
	    dp = dp->df_next;
	}
    }

    dprev = NULL;
    dp = first_diff;
    while (dp != NULL)
    {
	/* All counts are zero, remove this entry. */
	for (i = 0; i < DB_COUNT; ++i)
	    if (diffbuf[i] != NULL && dp->df_count[i] != 0)
		break;
	if (i == DB_COUNT)
	{
	    dnext = dp->df_next;
	    vim_free(dp);
	    dp = dnext;
	    if (dprev == NULL)
		first_diff = dnext;
	    else
		dprev->df_next = dnext;
	}
	else
	{
	    /* Advance to next entry. */
	    dprev = dp;
	    dp = dp->df_next;
	}

    }
    diff_redraw();
}

/*
 * Allocate a new diff block and link it between "dprev" and "dp".
 */
    static diff_T *
diff_alloc_new(dprev, dp)
    diff_T	*dprev;
    diff_T	*dp;
{
    diff_T	*dnew;

    dnew = (diff_T *)alloc((unsigned)sizeof(diff_T));
    if (dnew != NULL)
    {
	dnew->df_next = dp;
	if (dprev == NULL)
	    first_diff = dnew;
	else
	    dprev->df_next = dnew;
    }
    return dnew;
}

/*
 * Check if the diff block "dp" can be made smaller for lines at the start and
 * end that are equal.  Called after inserting lines.
 * This may result in a change where all buffers have zero lines, the caller
 * must take care of removing it.
 */
    static void
diff_check_unchanged(dp)
    diff_T	*dp;
{
    int		i_org;
    int		i_new;
    int		off_org, off_new;
    char_u	*line_org;
    int		dir = FORWARD;

    /* Find the first buffers, use it as the original, compare the other
     * buffer lines against this one. */
    for (i_org = 0; i_org < DB_COUNT; ++i_org)
	if (diffbuf[i_org] != NULL)
	    break;
    if (i_org == DB_COUNT)	/* safety check */
	return;

    /* First check lines at the top, then at the bottom. */
    off_org = 0;
    off_new = 0;
    for (;;)
    {
	/* Repeat until a line is found which is different or the number of
	 * lines has become zero. */
	while (dp->df_count[i_org] > 0)
	{
	    /* Copy the line, the next ml_get() will invalidate it.  */
	    if (dir == BACKWARD)
		off_org = dp->df_count[i_org] - 1;
	    line_org = vim_strsave(ml_get_buf(diffbuf[i_org],
					dp->df_lnum[i_org] + off_org, FALSE));
	    if (line_org == NULL)
		return;
	    for (i_new = i_org + 1; i_new < DB_COUNT; ++i_new)
	    {
		if (diffbuf[i_new] == NULL)
		    continue;
		if (dir == BACKWARD)
		    off_new = dp->df_count[i_new] - 1;
		/* if other buffer doesn't have this line, it was inserted */
		if (off_new < 0 || off_new >= dp->df_count[i_new])
		    break;
		if (diff_cmp(line_org, ml_get_buf(diffbuf[i_new],
				   dp->df_lnum[i_new] + off_new, FALSE)) != 0)
		    break;
	    }
	    vim_free(line_org);

	    /* Stop when a line isn't equal in all diff buffers. */
	    if (i_new != DB_COUNT)
		break;

	    /* Line matched in all buffers, remove it from the diff. */
	    for (i_new = i_org; i_new < DB_COUNT; ++i_new)
		if (diffbuf[i_new] != NULL)
		{
		    if (dir == FORWARD)
			++dp->df_lnum[i_new];
		    --dp->df_count[i_new];
		}
	}
	if (dir == BACKWARD)
	    break;
	dir = BACKWARD;
    }
}

/*
 * Mark all diff buffers for redraw.
 */
    void
diff_redraw()
{
    win_T	*wp;

    for (wp = firstwin; wp != NULL; wp = wp->w_next)
	if (wp->w_p_diff)
	{
	    redraw_win_later(wp, NOT_VALID);
#ifdef FEAT_FOLDING
	    if (foldmethodIsDiff(wp))
		foldUpdateAll(wp);
#endif
	}
}

/*
 * Completely update the diffs for the buffers involved.
 * This uses the ordinary "diff" command.
 * The buffers are written to a file, also for unmodified buffers (the file
 * could have been produced by autocommands, e.g. the netrw plugin).
 */
/*ARGSUSED*/
    void
ex_diffupdate(eap)
    exarg_T	*eap;
{
    buf_T	*buf;
    int		idx_orig;
    int		idx_new;
    char_u	*tmp_orig;
    char_u	*tmp_new;
    char_u	*tmp_diff;
    char_u	*cmd;

    /* Delete all diffblocks. */
    diff_clear();
    diff_invalid = FALSE;

    /* We need three temp file names. */
    tmp_orig = vim_tempname('o');
    tmp_new = vim_tempname('n');
    tmp_diff = vim_tempname('d');
    if (tmp_orig == NULL || tmp_new == NULL || tmp_diff == NULL)
	goto theend;

    /* Use the first buffer as the original text. */
    for (idx_orig = 0; idx_orig < DB_COUNT; ++idx_orig)
	if (diffbuf[idx_orig] != NULL)
	    break;
    if (idx_orig == DB_COUNT)
	goto theend;		/* no diff buffers */

    /* Write the first buffer to a tempfile. */
    buf = diffbuf[idx_orig];
    if (buf_write(buf, tmp_orig, NULL, (linenr_T)1, buf->b_ml.ml_line_count,
				     NULL, FALSE, FALSE, FALSE, TRUE) == FAIL)
	goto theend;

    /* Make a difference between the first buffer and every other. */
    for (idx_new = idx_orig + 1; idx_new < DB_COUNT; ++idx_new)
    {
	buf = diffbuf[idx_new];
	if (buf == NULL)
	    continue;
	if (buf_write(buf, tmp_new, NULL, (linenr_T)1, buf->b_ml.ml_line_count,
				     NULL, FALSE, FALSE, FALSE, TRUE) == FAIL)
	    continue;
#ifdef FEAT_EVAL
	if (*p_dex != NUL)
	    /* Use 'diffexpr' to generate the diff file. */
	    eval_diff(tmp_orig, tmp_new, tmp_diff);
	else
#endif
	{
	    cmd = alloc((unsigned)(STRLEN(tmp_orig) + STRLEN(tmp_new)
				    + STRLEN(tmp_diff) + STRLEN(p_srr) + 14));
	    if (cmd != NULL)
	    {
		/* Build the diff command and execute it.  Ignore errors, diff
		 * returns non-zero when differences have been found. */
		sprintf((char *)cmd, "diff %s%s%s %s",
			(diff_flags & DIFF_IWHITE) ? "-b " : "",
			(diff_flags & DIFF_ICASE) ? "-i " : "",
			tmp_orig, tmp_new);
		append_redir(cmd, tmp_diff);
		(void)call_shell(cmd, SHELL_FILTER|SHELL_SILENT|SHELL_DOOUT);
		vim_free(cmd);
	    }
	}

	/* Read the diff output and add each entry to the diff list. */
	diff_read(idx_orig, idx_new, tmp_diff);
	mch_remove(tmp_diff);
	mch_remove(tmp_new);
    }
    mch_remove(tmp_orig);

    diff_redraw();

theend:
    vim_free(tmp_orig);
    vim_free(tmp_new);
    vim_free(tmp_diff);
}

/*
 * Create a new version of a file from the current buffer and a diff file.
 * The buffer is written to a file, also for unmodified buffers (the file
 * could have been produced by autocommands, e.g. the netrw plugin).
 */
    void
ex_diffpatch(eap)
    exarg_T	*eap;
{
    char_u	*tmp_orig;	/* name of original temp file */
    char_u	*tmp_new;	/* name of patched temp file */
    char_u	*buf = NULL;
    win_T	*old_curwin = curwin;
    char_u	*newname = NULL;	/* name of patched file buffer */
#ifdef UNIX
    char_u	dirbuf[MAXPATHL];
#endif

    /* We need two temp file names. */
    tmp_orig = vim_tempname('o');
    tmp_new = vim_tempname('n');
    if (tmp_orig == NULL || tmp_new == NULL)
	goto theend;

    /* Write the current buffer to "tmp_orig". */
    if (buf_write(curbuf, tmp_orig, NULL,
		(linenr_T)1, curbuf->b_ml.ml_line_count,
				     NULL, FALSE, FALSE, FALSE, TRUE) == FAIL)
	goto theend;

    buf = alloc((unsigned)(STRLEN(tmp_orig) + STRLEN(eap->arg)
						     + STRLEN(tmp_new) + 14));
    if (buf == NULL)
	goto theend;

#ifdef UNIX
    /* Temporaraly chdir to /tmp, to avoid patching files in the current
     * directory when the patch file contains more than one patch.  When we
     * have our own temp dir use that instead, it will be cleaned up when we
     * exit (any .rej files created). */
    if (mch_dirname(dirbuf, MAXPATHL) != OK)
	dirbuf[0] = NUL;
    else
#ifdef TEMPDIRNAMES
	if (vim_tempdir != NULL)
	    mch_chdir((char *)vim_tempdir);
	else
#endif
	    mch_chdir("/tmp");
#endif

#ifdef FEAT_EVAL
    if (*p_pex != NUL)
	/* Use 'patchexpr' to generate the new file. */
	eval_patch(tmp_orig, eap->arg, tmp_new);
    else
#endif
    {
	/* Build the patch command and execute it.  Ignore errors.  Switch to
	 * cooked mode to allow the user to respond to prompts. */
	sprintf((char *)buf, "patch -o %s %s < \"%s\"",
		tmp_new, tmp_orig, eap->arg);
	(void)call_shell(buf, SHELL_FILTER | SHELL_COOKED);
    }

#ifdef UNIX
    if (dirbuf[0] != NUL)
	mch_chdir((char *)dirbuf);
#endif

    /* patch probably has written over the screen */
    redraw_later(CLEAR);

    /* Delete any .orig or .rej file created. */
    STRCPY(buf, tmp_new);
    STRCAT(buf, ".orig");
    mch_remove(buf);
    STRCPY(buf, tmp_new);
    STRCAT(buf, ".rej");
    mch_remove(buf);

    if (curbuf->b_fname != NULL)
    {
	newname = vim_strnsave(curbuf->b_fname,
					  (int)(STRLEN(curbuf->b_fname) + 4));
	if (newname != NULL)
	    STRCAT(newname, ".new");
    }

#ifdef FEAT_GUI
    need_mouse_correct = TRUE;
#endif
    if (win_split(0, 0) != FAIL)
    {
	/* Pretend it was a ":split fname" command */
	eap->cmdidx = CMD_split;
	eap->arg = tmp_new;
	do_exedit(eap, old_curwin);

	if (curwin != old_curwin)		/* split must have worked */
	{
	    /* Set 'diff', 'scrollbind' on and 'wrap' off. */
	    diff_win_options(curwin, TRUE);
	    diff_win_options(old_curwin, TRUE);

	    if (newname != NULL)
	    {
		/* do a ":file filename.new" on the patched buffer */
		eap->arg = newname;
		ex_file(eap);

#ifdef FEAT_AUTOCMD
		/* Do filetype detection with the new name. */
		do_cmdline_cmd((char_u *)":doau filetypedetect BufRead");
#endif
	    }
	}
    }

theend:
    if (tmp_orig != NULL)
	mch_remove(tmp_orig);
    vim_free(tmp_orig);
    if (tmp_new != NULL)
	mch_remove(tmp_new);
    vim_free(tmp_new);
    vim_free(newname);
    vim_free(buf);
}

/*
 * Split the window and edit another file, setting options to show the diffs.
 */
    void
ex_diffsplit(eap)
    exarg_T	*eap;
{
    win_T	*old_curwin = curwin;

#ifdef FEAT_GUI
    need_mouse_correct = TRUE;
#endif
    if (win_split(0, 0) != FAIL)
    {
	/* Pretend it was a ":split fname" command */
	eap->cmdidx = CMD_split;
	do_exedit(eap, old_curwin);

	if (curwin != old_curwin)		/* split must have worked */
	{
	    /* Set 'diff', 'scrollbind' on and 'wrap' off. */
	    diff_win_options(curwin, TRUE);
	    diff_win_options(old_curwin, TRUE);
	}
    }
}

/*
 * Set options in window "wp" for diff mode.
 */
    void
diff_win_options(wp, addbuf)
    win_T	*wp;
    int		addbuf;		/* Add buffer to diff. */
{
    wp->w_p_diff = TRUE;
    wp->w_p_scb = TRUE;
    wp->w_p_wrap = FALSE;
# ifdef FEAT_FOLDING
    {
	win_T	    *old_curwin = curwin;

	curwin = wp;
	curbuf = curwin->w_buffer;
	set_string_option_direct((char_u *)"fdm", -1, (char_u *)"diff",
								   OPT_LOCAL);
	curwin = old_curwin;
	curbuf = curwin->w_buffer;
	wp->w_p_fdc = 2;
	wp->w_p_fen = TRUE;
	wp->w_p_fdl = 0;
    }
# endif
    if (addbuf)
	diff_buf_add(wp->w_buffer);
}

/*
 * Read the diff output and add each entry to the diff list.
 */
    static void
diff_read(idx_orig, idx_new, fname)
    int		idx_orig;	/* idx of original file */
    int		idx_new;	/* idx of new file */
    char_u	*fname;		/* name of diff output file */
{
    FILE	*fd;
    diff_T	*dprev = NULL;
    diff_T	*dp = first_diff;
    diff_T	*dn, *dpl;
    long	f1, l1, f2, l2;
#define LBUFLEN 50
    char_u	linebuf[LBUFLEN];   /* only need to hold the diff line */
    int		difftype;
    char_u	*p;
    long	off;
    int		i;
    linenr_T	lnum_orig, lnum_new;
    int		count_orig, count_new;
    int		notset = TRUE;	    /* block "*dp" not set yet */

    fd = fopen((char *)fname, "r");
    if (fd == NULL)
	return;

    for (;;)
    {
	if (tag_fgets(linebuf, LBUFLEN, fd))
	    break;		/* end of file */
	if (!isdigit(*linebuf))
	    continue;		/* not the start of a diff block */

	/* This line must be one of three formats:
	 * {first}[,{last}]c{first}[,{last}]
	 * {first}a{first}[,{last}]
	 * {first}[,{last}]d{first}
	 */
	p = linebuf;
	f1 = getdigits(&p);
	if (*p == ',')
	{
	    ++p;
	    l1 = getdigits(&p);
	}
	else
	    l1 = f1;
	if (*p != 'a' && *p != 'c' && *p != 'd')
	    continue;		/* invalid diff format */
	difftype = *p++;
	f2 = getdigits(&p);
	if (*p == ',')
	{
	    ++p;
	    l2 = getdigits(&p);
	}
	else
	    l2 = f2;
	if (l1 < f1 || l2 < f2)
	    continue;		/* invalid line range */

	if (difftype == 'a')
	{
	    lnum_orig = f1 + 1;
	    count_orig = 0;
	}
	else
	{
	    lnum_orig = f1;
	    count_orig = l1 - f1 + 1;
	}
	if (difftype == 'd')
	{
	    lnum_new = f2 + 1;
	    count_new = 0;
	}
	else
	{
	    lnum_new = f2;
	    count_new = l2 - f2 + 1;
	}

	/* Go over blocks before the change, for which orig and new are equal */
	while (dp != NULL
		&& lnum_orig > dp->df_lnum[idx_orig] + dp->df_count[idx_orig])
	{
	    if (notset)
		diff_copy_entry(dprev, dp, idx_orig, idx_new);
	    dprev = dp;
	    dp = dp->df_next;
	    notset = TRUE;
	}

	if (dp != NULL
		&& lnum_orig <= dp->df_lnum[idx_orig] + dp->df_count[idx_orig]
		&& lnum_orig + count_orig >= dp->df_lnum[idx_orig])
	{
	    /* New block overlaps with existing block(s).
	     * First find last block that overlaps. */
	    for (dpl = dp; dpl->df_next != NULL; dpl = dpl->df_next)
		if (lnum_orig + count_orig < dpl->df_next->df_lnum[idx_orig])
		    break;

	    /* If the newly found block starts before the old one, set the
	     * start back a number of lines. */
	    off = dp->df_lnum[idx_orig] - lnum_orig;
	    if (off > 0)
	    {
		for (i = idx_orig; i < idx_new; ++i)
		    if (diffbuf[i] != NULL)
			dp->df_lnum[i] -= off;
		dp->df_lnum[idx_new] = lnum_new;
		dp->df_count[idx_new] = count_new;
	    }
	    else if (notset)
	    {
		/* new block inside existing one, adjust new block */
		dp->df_lnum[idx_new] = lnum_new + off;
		dp->df_count[idx_new] = count_new - off;
	    }
	    else
		/* second overlap of new block with existing block */
		dp->df_count[idx_new] += count_new - count_orig;

	    /* Adjust the size of the block to include all the lines to the
	     * end of the existing block or the new diff, whatever ends last. */
	    off = (lnum_orig + count_orig)
			 - (dpl->df_lnum[idx_orig] + dpl->df_count[idx_orig]);
	    if (off < 0)
	    {
		/* new change ends in existing block, adjust the end if not
		 * done already */
		if (notset)
		    dp->df_count[idx_new] += -off;
		off = 0;
	    }
	    for (i = idx_orig; i < idx_new + !notset; ++i)
		if (diffbuf[i] != NULL)
		    dp->df_count[i] = dpl->df_lnum[i] + dpl->df_count[i]
						       - dp->df_lnum[i] + off;

	    /* Delete the diff blocks that have been merged into one. */
	    dn = dp->df_next;
	    dp->df_next = dpl->df_next;
	    while (dn != dp->df_next)
	    {
		dpl = dn->df_next;
		vim_free(dn);
		dn = dpl;
	    }
	}
	else
	{
	    /* Allocate a new diffblock. */
	    dp = diff_alloc_new(dprev, dp);
	    if (dp == NULL)
		return;

	    dp->df_lnum[idx_orig] = lnum_orig;
	    dp->df_count[idx_orig] = count_orig;
	    dp->df_lnum[idx_new] = lnum_new;
	    dp->df_count[idx_new] = count_new;

	    /* Set values for other buffers, these must be equal to the
	     * original buffer, otherwise there would have been a change
	     * already. */
	    for (i = idx_orig + 1; i < idx_new; ++i)
		diff_copy_entry(dprev, dp, idx_orig, i);
	}
	notset = FALSE;		/* "*dp" has been set */
    }

    /* for remaining diff blocks orig and new are equal */
    while (dp != NULL)
    {
	if (notset)
	    diff_copy_entry(dprev, dp, idx_orig, idx_new);
	dprev = dp;
	dp = dp->df_next;
	notset = TRUE;
    }
}

/*
 * Copy an entry at "dp" from "idx_orig" to "idx_new".
 */
    static void
diff_copy_entry(dprev, dp, idx_orig, idx_new)
    diff_T	*dprev;
    diff_T	*dp;
    int		idx_orig;
    int		idx_new;
{
    int		off;

    if (dprev == NULL)
	off = 0;
    else
	off = (dprev->df_lnum[idx_orig] + dprev->df_count[idx_orig])
	    - (dprev->df_lnum[idx_new] + dprev->df_count[idx_new]);
    dp->df_lnum[idx_new] = dp->df_lnum[idx_orig] - off;
    dp->df_count[idx_new] = dp->df_count[idx_orig];
}

/*
 * Clear the list of diffblocks.
 */
    static void
diff_clear()
{
    diff_T	*p, *next_p;

    for (p = first_diff; p != NULL; p = next_p)
    {
	next_p = p->df_next;
	vim_free(p);
    }
    first_diff = NULL;
}

/*
 * Check diff status for line "lnum" in buffer "buf":
 * Returns 0 for nothing special
 * Returns -1 for a line that should be highlighted as changed.
 * Returns -2 for a line that should be highlighted as added/deleted.
 * Returns > 0 for inserting that many filler lines above it (never happens
 * when 'diffopt' doesn't contain "filler").
 * This should only be used for windows where 'diff' is set.
 */
    int
diff_check(wp, lnum)
    win_T	*wp;
    linenr_T	lnum;
{
    int		idx;		/* index in diffbuf[] for this buffer */
    diff_T	*dp;
    int		maxcount;
    int		i;
    buf_T	*buf = wp->w_buffer;
    int		cmp;

    if (diff_invalid)
	ex_diffupdate(NULL);		/* update after a big change */

    if (first_diff == NULL || !wp->w_p_diff)	/* no diffs at all */
	return 0;

    /* safety check: "lnum" must be a buffer line */
    if (lnum < 1 || lnum > buf->b_ml.ml_line_count)
	return 0;

    idx = diff_buf_idx(buf);
    if (idx == DB_COUNT)
	return 0;		/* no diffs for buffer "buf" */

#ifdef FEAT_FOLDING
    /* A closed fold never has filler lines. */
    if (hasFoldingWin(wp, lnum, NULL, NULL, TRUE, NULL))
	return 0;
#endif

    /* search for a change that includes "lnum" in the list of diffblocks. */
    for (dp = first_diff; dp != NULL; dp = dp->df_next)
	if (lnum <= dp->df_lnum[idx] + dp->df_count[idx])
	    break;
    if (dp == NULL || lnum < dp->df_lnum[idx])
	return 0;

    if (lnum < dp->df_lnum[idx] + dp->df_count[idx])
    {
	/* Changed or inserted line.  If the other buffers have a count of
	 * zero, the lines were inserted.  If the other buffers have the same
	 * count, check if the lines are identical. */
	cmp = FALSE;
	for (i = 0; i < DB_COUNT; ++i)
	    if (i != idx && diffbuf[i] != NULL && dp->df_count[i] != 0)
	    {
		if (dp->df_count[i] != dp->df_count[idx])
		    return -1;	    /* nr of lines changed. */
		cmp = TRUE;
	    }
	if (cmp)
	{
	    /* Compare all lines.  If they are equal the lines were inserted
	     * in some buffers, deleted in others, but not changed. */
	    for (i = 0; i < DB_COUNT; ++i)
		if (i != idx && diffbuf[i] != NULL && dp->df_count[i] != 0)
		    if (!diff_equal_entry(dp, idx, i))
			return -1;
	}
	return -2;
    }

    /* If 'diffopt' doesn't contain "filler", return 0. */
    if (!(diff_flags & DIFF_FILLER))
	return 0;

    /* Insert filler lines above the line just below the change.  Will return
     * 0 when this buf had the max count. */
    maxcount = 0;
    for (i = 0; i < DB_COUNT; ++i)
	if (diffbuf[i] != NULL && dp->df_count[i] > maxcount)
	    maxcount = dp->df_count[i];
    return maxcount - dp->df_count[idx];
}

/*
 * Compare two entries in diff "*dp" and return TRUE if they are equal.
 */
    static int
diff_equal_entry(dp, idx1, idx2)
    diff_T	*dp;
    int		idx1;
    int		idx2;
{
    int		i;
    char_u	*line;
    int		cmp;

    if (dp->df_count[idx1] != dp->df_count[idx2])
	return FALSE;
    for (i = 0; i < dp->df_count[idx1]; ++i)
    {
	line = vim_strsave(ml_get_buf(diffbuf[idx1],
					       dp->df_lnum[idx1] + i, FALSE));
	if (line == NULL)
	    return FALSE;
	cmp = diff_cmp(line, ml_get_buf(diffbuf[idx2],
					       dp->df_lnum[idx2] + i, FALSE));
	vim_free(line);
	if (cmp != 0)
	    return FALSE;
    }
    return TRUE;
}

/*
 * Compare strings "s1" and "s2" according to 'diffopt'.
 * Return non-zero when they are different.
 */
    static int
diff_cmp(s1, s2)
    char_u	*s1;
    char_u	*s2;
{
    char_u	*p1, *p2;
#ifdef FEAT_MBYTE
    int		l;
#endif

    if ((diff_flags & (DIFF_ICASE | DIFF_IWHITE)) == 0)
	return STRCMP(s1, s2);
    if ((diff_flags & DIFF_ICASE) && !(diff_flags & DIFF_IWHITE))
	return STRICMP(s1, s2);

    /* Ignore case AND ignore white space changes. */
    p1 = s1;
    p2 = s2;
    while (*p1 != NUL && *p2 != NUL)
    {
	if (vim_iswhite(*p1) && vim_iswhite(*p2))
	{
	    p1 = skipwhite(p1);
	    p2 = skipwhite(p2);
	}
	else
	{
#ifdef FEAT_MBYTE
	    l  = (*mb_ptr2len_check)(p1);
	    if (l != (*mb_ptr2len_check)(p2))
		break;
	    if (l > 1)
	    {
		if (STRNCMP(p1, p2, l) != 0)
		    break;
		p1 += l;
		p2 += l;
	    }
	    else
#endif
	    {
		if (*p1 != *p2 && TO_LOWER(*p1) != TO_LOWER(*p2))
		    break;
		++p1;
		++p2;
	    }
	}
    }

    /* Ignore trailing white space. */
    p1 = skipwhite(p1);
    p2 = skipwhite(p2);
    if (*p1 != NUL || *p2 != NUL)
	return 1;
    return 0;
}

/*
 * Return the number of filler lines above "lnum".
 */
    int
diff_check_fill(wp, lnum)
    win_T	*wp;
    linenr_T	lnum;
{
    int		n;

    /* be quick when there are no filler lines */
    if (!(diff_flags & DIFF_FILLER))
	return 0;
    n = diff_check(wp, lnum);
    if (n <= 0)
	return 0;
    return n;
}

/*
 * Set the topline of "towin" to match the position in "fromwin", so that they
 * show the same diff'ed lines.
 */
    void
diff_set_topline(fromwin, towin)
    win_T	*fromwin;
    win_T	*towin;
{
    buf_T	*buf = fromwin->w_buffer;
    linenr_T	lnum = fromwin->w_topline;
    int		idx;
    diff_T	*dp;
    int		i;

    idx = diff_buf_idx(buf);
    if (idx == DB_COUNT)
	return;		/* safety check */

    if (diff_invalid)
	ex_diffupdate(NULL);		/* update after a big change */

    towin->w_topfill = 0;

    /* search for a change that includes "lnum" in the list of diffblocks. */
    for (dp = first_diff; dp != NULL; dp = dp->df_next)
	if (lnum <= dp->df_lnum[idx] + dp->df_count[idx])
	    break;
    if (dp == NULL)
    {
	/* After last change, compute topline relative to end of file; no
	 * filler lines. */
	towin->w_topline = towin->w_buffer->b_ml.ml_line_count
					   - (buf->b_ml.ml_line_count - lnum);
    }
    else
    {
	/* Find index for "towin". */
	i = diff_buf_idx(towin->w_buffer);
	if (i == DB_COUNT)
	    return;		/* safety check */

	towin->w_topline = lnum + (dp->df_lnum[i] - dp->df_lnum[idx]);
	if (lnum >= dp->df_lnum[idx])
	{
	    /* Inside a change: compute filler lines. */
	    if (dp->df_count[i] == dp->df_count[idx])
		towin->w_topfill = fromwin->w_topfill;
	    else if (dp->df_count[i] > dp->df_count[idx])
	    {
		if (lnum == dp->df_lnum[idx] + dp->df_count[idx])
		    towin->w_topline = dp->df_lnum[i] + dp->df_count[i]
							 - fromwin->w_topfill;
	    }
	    else
	    {
		if (towin->w_topline >= dp->df_lnum[i] + dp->df_count[i])
		{
		    if (diff_flags & DIFF_FILLER)
			towin->w_topfill = dp->df_lnum[idx]
						   + dp->df_count[idx] - lnum;
		    towin->w_topline = dp->df_lnum[i] + dp->df_count[i];
		}
	    }
	}
    }

    /* safety check (if diff info gets outdated strange things may happen) */
    if (towin->w_topline > towin->w_buffer->b_ml.ml_line_count)
    {
	towin->w_topline = towin->w_buffer->b_ml.ml_line_count;
	towin->w_topfill = 0;
    }
    if (towin->w_topline < 1)
    {
	towin->w_topline = 1;
	towin->w_topfill = 0;
    }
#ifdef FEAT_FOLDING
    (void)hasFoldingWin(towin, towin->w_topline, &towin->w_topline,
							    NULL, TRUE, NULL);
#endif
}

/*
 * This is called when 'diffopt' is changed.
 */
    int
diffopt_changed()
{
    char_u	*p;
    int		diff_context_new = 6;
    int		diff_flags_new = 0;

    p = p_dip;
    while (*p != NUL)
    {
	if (STRNCMP(p, "filler", 6) == 0)
	{
	    p += 6;
	    diff_flags_new |= DIFF_FILLER;
	}
	else if (STRNCMP(p, "context:", 8) == 0 && isdigit(p[8]))
	{
	    p += 8;
	    diff_context_new = getdigits(&p);
	}
	else if (STRNCMP(p, "icase", 5) == 0)
	{
	    p += 5;
	    diff_flags_new |= DIFF_ICASE;
	}
	else if (STRNCMP(p, "iwhite", 6) == 0)
	{
	    p += 6;
	    diff_flags_new |= DIFF_IWHITE;
	}
	if (*p != ',' && *p != NUL)
	    return FAIL;
	if (*p == ',')
	    ++p;
    }

    /* If "icase" or "iwhite" was added or removed, need to update the diff. */
    if (diff_flags != diff_flags_new)
	diff_invalid = TRUE;

    diff_flags = diff_flags_new;
    diff_context = diff_context_new;

    diff_redraw();

    /* recompute the scroll binding with the new option value, may
     * remove or add filler lines */
    check_scrollbind((linenr_T)0, 0L);

    return OK;
}

/*
 * Find the difference within a changed line.
 */
    void
diff_find_change(wp, lnum, startp, endp)
    win_T	*wp;
    linenr_T	lnum;
    int		*startp;	/* first char of the change */
    int		*endp;		/* last char of the change */
{
    char_u	*line_org;
    char_u	*line_new;
    int		i;
    int		si, ei_org, ei_new;
    diff_T	*dp;
    int		idx;
    int		off;

    /* Make a copy of the line, the next ml_get() will invalidate it. */
    line_org = vim_strsave(ml_get_buf(wp->w_buffer, lnum, FALSE));
    if (line_org == NULL)
	return;

    idx = diff_buf_idx(wp->w_buffer);
    if (idx == DB_COUNT)	/* cannot happen */
	return;

    /* search for a change that includes "lnum" in the list of diffblocks. */
    for (dp = first_diff; dp != NULL; dp = dp->df_next)
	if (lnum <= dp->df_lnum[idx] + dp->df_count[idx])
	    break;
    off = lnum - dp->df_lnum[idx];

    for (i = 0; i < DB_COUNT; ++i)
	if (diffbuf[i] != NULL && i != idx)
	{
	    /* Skip lines that are not in the other change (filler lines). */
	    if (off >= dp->df_count[i])
		continue;
	    line_new = ml_get_buf(diffbuf[i], dp->df_lnum[i] + off, FALSE);

	    /* Search for start of difference */
	    for (si = 0; line_org[si] != NUL && line_org[si] == line_new[si]; )
		++si;
	    if (*startp > si)
		*startp = si;

	    /* Search for end of difference, if any. */
	    if (line_org[si] != NUL || line_new[si] != NUL)
	    {
		ei_org = STRLEN(line_org);
		ei_new = STRLEN(line_new);
		while (ei_org >= *startp && ei_new >= *startp
			&& ei_org >= 0 && ei_new >= 0
			&& line_org[ei_org] == line_new[ei_new])
		{
		    --ei_org;
		    --ei_new;
		}
		if (*endp < ei_org)
		    *endp = ei_org;
	    }
	}

    vim_free(line_org);
}

#if defined(FEAT_FOLDING) || defined(PROTO)
/*
 * Return TRUE if line "lnum" is not close to a diff block, this line should
 * be in a fold.
 * Return FALSE if there are no diff blocks at all in this window.
 */
    int
diff_infold(wp, lnum)
    win_T	*wp;
    linenr_T	lnum;
{
    int		i;
    int		idx = -1;
    int		other = FALSE;
    diff_T	*dp;

    /* Return if 'diff' isn't set. */
    if (!wp->w_p_diff)
	return FALSE;

    for (i = 0; i < DB_COUNT; ++i)
    {
	if (diffbuf[i] == wp->w_buffer)
	    idx = i;
	else if (diffbuf[i] != NULL)
	    other = TRUE;
    }

    /* return here if there are no diffs in the window */
    if (idx == -1 || !other)
	return FALSE;

    if (diff_invalid)
	ex_diffupdate(NULL);		/* update after a big change */

    /* Return if there are no diff blocks. */
    if (first_diff == NULL)
	return FALSE;

    for (dp = first_diff; dp != NULL; dp = dp->df_next)
    {
	/* If this change is below the line there can't be any further match. */
	if (dp->df_lnum[idx] - diff_context > lnum)
	    break;
	/* If this change ends before the line we have a match. */
	if (dp->df_lnum[idx] + dp->df_count[idx] + diff_context > lnum)
	    return FALSE;
    }
    return TRUE;
}
#endif

/*
 * ":diffget"
 * ":diffput"
 */
    void
ex_diffgetput(eap)
    exarg_T	*eap;
{
    linenr_T	lnum;
    int		count;
    linenr_T	off = 0;
    diff_T	*dp;
    diff_T	*dprev;
    diff_T	*dfree;
    int		idx_cur;
    int		idx_other;
    int		idx_from;
    int		idx_to;
    int		i;
    int		added;
    char_u	*p;
    win_T	*wp;
    aco_save_T	aco;
    buf_T	*buf;

    /* Find the current buffer in the list of diff buffers. */
    idx_cur = diff_buf_idx(curbuf);
    if (idx_cur == DB_COUNT)
    {
	EMSG(_("Current buffer is not in diff mode"));
	return;
    }

    if (*eap->arg == NUL)
    {
	/* No argument: Find the other buffer in the list of diff buffers. */
	for (idx_other = 0; idx_other < DB_COUNT; ++idx_other)
	    if (diffbuf[idx_other] != curbuf && diffbuf[idx_other] != NULL)
		break;
	if (idx_other == DB_COUNT)
	{
	    EMSG(_("No other buffer in diff mode"));
	    return;
	}

	/* Check that there isn't a third buffer in the list */
	for (i = idx_other + 1; i < DB_COUNT; ++i)
	    if (diffbuf[i] != curbuf && diffbuf[i] != NULL)
	    {
		EMSG(_("More than two buffers in diff mode, don't know which one to use"));
		return;
	    }
    }
    else
    {
	/* Buffer number or pattern given.  Ignore trailing white space. */
	p = eap->arg + STRLEN(eap->arg);
	while (p > eap->arg && vim_iswhite(p[-1]))
	    --p;
	for (i = 0; isdigit(eap->arg[i]) && eap->arg + i < p; ++i)
	    ;
	if (eap->arg + i == p)	    /* digits only */
	    i = atol((char *)eap->arg);
	else
	{
	    i = buflist_findpat(eap->arg, p, FALSE, TRUE);
	    if (i < 0)
		return;		/* error message already given */
	}
	buf = buflist_findnr(i);
	if (buf == NULL)
	{
	    EMSG2(_("Can't find buffer \"%s\""), eap->arg);
	    return;
	}
	idx_other = diff_buf_idx(buf);
	if (idx_other == DB_COUNT)
	{
	    EMSG2(_("Buffer \"%s\" is not in diff mode"), eap->arg);
	    return;
	}
    }

    diff_busy = TRUE;

    /* When no range given include the line above the cursor. */
    if (eap->addr_count == 0 && eap->line1 > 1)
	--eap->line1;

    if (eap->cmdidx == CMD_diffget)
    {
	idx_from = idx_other;
	idx_to = idx_cur;
    }
    else
    {
	idx_from = idx_cur;
	idx_to = idx_other;
	/* Need to make the other buffer the current buffer to be able to make
	 * changes in it. */
	/* set curwin/curbuf to buf and save a few things */
	aucmd_prepbuf(&aco, diffbuf[idx_other]);
    }

    dprev = NULL;
    for (dp = first_diff; dp != NULL; )
    {
	if (dp->df_lnum[idx_cur] > eap->line2 + off)
	    break;	/* past the range that was specified */

	dfree = NULL;
	lnum = dp->df_lnum[idx_to];
	count = dp->df_count[idx_to];
	if (dp->df_lnum[idx_cur] + dp->df_count[idx_cur] > eap->line1 + off
		&& u_save(lnum - 1, lnum + count) != FAIL)
	{
	    /* Inside the specified range and saving for undo worked. */
	    for (i = 0; i < count; ++i)
		ml_delete(lnum, FALSE);
	    for (i = 0; i < dp->df_count[idx_from]; ++i)
	    {
		p = vim_strsave(ml_get_buf(diffbuf[idx_from],
					    dp->df_lnum[idx_from] + i, FALSE));
		ml_append(lnum + i - 1, p, 0, FALSE);
	    }

	    added = dp->df_count[idx_from] - count;

	    /* Check if there are any other buffers and if the diff is equal
	     * in them. */
	    for (i = 0; i < DB_COUNT; ++i)
		if (diffbuf[i] != NULL && i != idx_from && i != idx_to
			&& !diff_equal_entry(dp, idx_from, i))
		    break;
	    if (i == DB_COUNT)
	    {
		/* delete the diff entry, the buffers are now equal here */
		dfree = dp;
		dp = dp->df_next;
		if (dprev == NULL)
		    first_diff = dp;
		else
		    dprev->df_next = dp;
	    }

	    /* Adjust marks.  This will change the following entries! */
	    if (added != 0)
		mark_adjust(lnum, lnum + count, (long)MAXLNUM, (long)added);
	    changed_lines(lnum, 0, lnum + count, (long)added);

	    if (dfree != NULL)
	    {
		/* Diff is deleted, update folds in other windows. */
		for (wp = firstwin; wp != NULL; wp = wp->w_next)
		    for (i = 0; i < DB_COUNT; ++i)
			if (diffbuf[i] == wp->w_buffer && i != idx_to)
			    foldUpdate(wp, dfree->df_lnum[i],
				      dfree->df_lnum[i] + dfree->df_count[i]);
		vim_free(dfree);
	    }
	    else
		/* mark_adjust() may have changed the count */
		dp->df_count[idx_to] = dp->df_count[idx_from];

	    /* When changing the current buffer, keep track of line numbers */
	    if (idx_cur == idx_to)
		off += added;
	}

	/* If before the range or not deleted, go to next diff. */
	if (dfree == NULL)
	{
	    dprev = dp;
	    dp = dp->df_next;
	}
    }

    /* restore curwin/curbuf and a few other things */
    if (idx_other == idx_to)
	aucmd_restbuf(&aco);

    diff_busy = FALSE;

    /* Also need to redraw the "from" buffer. */
    redraw_buf_later(diffbuf[idx_from], NOT_VALID);
}

/*
 * Return TRUE if buffer "buf" is in diff-mode.
 */
    int
diff_mode_buf(buf)
    buf_T	*buf;
{
    return diff_buf_idx(buf) != DB_COUNT;
}

#endif	/* FEAT_DIFF */
