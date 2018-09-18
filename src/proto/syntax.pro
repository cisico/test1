/* syntax.c */
void syntax_start __ARGS((win_T *wp, linenr_T lnum));
void syn_stack_free_all __ARGS((buf_T *buf));
void syn_stack_apply_changes __ARGS((buf_T *buf));
void syntax_end_parsing __ARGS((linenr_T lnum));
int syntax_check_changed __ARGS((linenr_T lnum));
int get_syntax_attr __ARGS((colnr_T col));
void syntax_clear __ARGS((buf_T *buf));
void ex_syntax __ARGS((exarg_T *eap));
int syntax_present __ARGS((buf_T *buf));
void set_context_in_syntax_cmd __ARGS((expand_T *xp, char_u *arg));
char_u *get_syntax_name __ARGS((expand_T *xp, int idx));
int syn_get_id __ARGS((long lnum, long col, int trans));
int syn_get_foldlevel __ARGS((win_T *wp, long lnum));
void init_highlight __ARGS((int both, int reset));
int load_colors __ARGS((char_u *p));
void do_highlight __ARGS((char_u *line, int forceit, int init));
void restore_cterm_colors __ARGS((void));
void set_normal_colors __ARGS((void));
char_u *hl_get_font_name __ARGS((void));
void hl_set_font_name __ARGS((char_u *font_name));
void hl_set_bg_color_name __ARGS((char_u *name));
void hl_set_fg_color_name __ARGS((char_u *name));
attrentry_T *syn_gui_attr2entry __ARGS((int attr));
attrentry_T *syn_term_attr2entry __ARGS((int attr));
attrentry_T *syn_cterm_attr2entry __ARGS((int attr));
char_u *highlight_has_attr __ARGS((int id, int flag, int modec));
char_u *highlight_color __ARGS((int id, char_u *what, int modec));
long_u highlight_gui_color_rgb __ARGS((int id, int fg));
int syn_name2id __ARGS((char_u *name));
int highlight_exists __ARGS((char_u *name));
int syn_namen2id __ARGS((char_u *linep, int len));
int syn_check_group __ARGS((char_u *pp, int len));
int syn_id2attr __ARGS((int hl_id));
int syn_id2colors __ARGS((int hl_id, guicolor_T *fgp, guicolor_T *bgp));
int syn_get_final_id __ARGS((int hl_id));
void highlight_gui_started __ARGS((void));
int highlight_changed __ARGS((void));
void set_context_in_highlight_cmd __ARGS((expand_T *xp, char_u *arg));
char_u *get_highlight_name __ARGS((expand_T *xp, int idx));
void free_highlight_fonts __ARGS((void));
/* vim: set ft=c : */
