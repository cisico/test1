/* mark.c */
int setmark __ARGS((int c));
void setpcmark __ARGS((void));
void checkpcmark __ARGS((void));
pos_T *movemark __ARGS((int count));
pos_T *getmark __ARGS((int c, int changefile));
pos_T *getnextmark __ARGS((pos_T *startpos, int dir, int begin_line));
void fmarks_check_names __ARGS((buf_T *buf));
int check_mark __ARGS((pos_T *pos));
void clrallmarks __ARGS((buf_T *buf));
char_u *fm_getname __ARGS((fmark_T *fmark, int lead_len));
void do_marks __ARGS((exarg_T *eap));
void ex_jumps __ARGS((exarg_T *eap));
void mark_adjust __ARGS((linenr_T line1, linenr_T line2, long amount, long amount_after));
void copy_jumplist __ARGS((win_T *from, win_T *to));
void free_jumplist __ARGS((win_T *wp));
void set_last_cursor __ARGS((win_T *win));
int read_viminfo_filemark __ARGS((vir_T *virp, int force));
void write_viminfo_filemarks __ARGS((FILE *fp));
int removable __ARGS((char_u *name));
int write_viminfo_marks __ARGS((FILE *fp_out));
void copy_viminfo_marks __ARGS((vir_T *virp, FILE *fp_out, int count, int eof));
/* vim: set ft=c : */
