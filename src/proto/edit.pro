/* edit.c */
int edit __ARGS((int cmdchar, int startln, long count));
void edit_putchar __ARGS((int c, int highlight));
void edit_unputchar __ARGS((void));
void display_dollar __ARGS((colnr_T col));
void change_indent __ARGS((int type, int amount, int round, int replaced));
void truncate_spaces __ARGS((char_u *line));
void backspace_until_column __ARGS((int col));
int vim_is_ctrl_x_key __ARGS((int c));
int ins_compl_add_infercase __ARGS((char_u *str, int len, char_u *fname, int dir, int reuse));
char_u *find_word_start __ARGS((char_u *ptr));
char_u *find_word_end __ARGS((char_u *ptr));
void ins_compl_check_keys __ARGS((void));
int get_literal __ARGS((void));
void insertchar __ARGS((int c, int flags, int second_indent));
void auto_format __ARGS((void));
int comp_textwidth __ARGS((int ff));
int stop_arrow __ARGS((void));
void set_last_insert __ARGS((int c));
char_u *add_char2buf __ARGS((int c, char_u *s));
void beginline __ARGS((int flags));
int oneright __ARGS((void));
int oneleft __ARGS((void));
int cursor_up __ARGS((long n, int upd_topline));
int cursor_down __ARGS((long n, int upd_topline));
int stuff_inserted __ARGS((int c, long count, int no_esc));
char_u *get_last_insert __ARGS((void));
char_u *get_last_insert_save __ARGS((void));
void replace_push __ARGS((int c));
void fixthisline __ARGS((int (*get_the_indent)(void)));
void fix_indent __ARGS((void));
int in_cinkeys __ARGS((int keytyped, int when, int line_is_empty));
int hkmap __ARGS((int c));
void ins_scroll __ARGS((void));
void ins_horscroll __ARGS((void));
/* vim: set ft=c : */