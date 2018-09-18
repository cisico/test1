/* mbyte.c */
int enc_canon_props __ARGS((char_u *name));
char_u *mb_init __ARGS((void));
int bomb_size __ARGS((void));
int mb_get_class __ARGS((char_u *p));
int dbcs_class __ARGS((unsigned lead, unsigned trail));
int latin_char2len __ARGS((int c));
int latin_char2bytes __ARGS((int c, char_u *buf));
int latin_ptr2len_check __ARGS((char_u *p));
int utf_char2cells __ARGS((int c));
int latin_ptr2cells __ARGS((char_u *p));
int utf_ptr2cells __ARGS((char_u *p));
int dbcs_ptr2cells __ARGS((char_u *p));
int latin_char2cells __ARGS((int c));
int latin_off2cells __ARGS((unsigned off));
int dbcs_off2cells __ARGS((unsigned off));
int utf_off2cells __ARGS((unsigned off));
int latin_ptr2char __ARGS((char_u *p));
int utf_ptr2char __ARGS((char_u *p));
int mb_ptr2char_adv __ARGS((char_u **pp));
int arabic_combine __ARGS((int one, int two));
int arabic_maycombine __ARGS((int two));
int utf_composinglike __ARGS((char_u *p1, char_u *p2));
int utfc_ptr2char __ARGS((char_u *p, int *p1, int *p2));
int utfc_ptr2char_len __ARGS((char_u *p, int *p1, int *p2, int maxlen));
int utfc_char2bytes __ARGS((int off, char_u *buf));
int utf_ptr2len_check __ARGS((char_u *p));
int utf_byte2len __ARGS((int b));
int utf_ptr2len_check_len __ARGS((char_u *p, int size));
int utfc_ptr2len_check __ARGS((char_u *p));
int utfc_ptr2len_check_len __ARGS((char_u *p, int size));
int utf_char2len __ARGS((int c));
int utf_char2bytes __ARGS((int c, char_u *buf));
int utf_iscomposing __ARGS((int c));
int utf_printable __ARGS((int c));
int utf_class __ARGS((int c));
int utf_fold __ARGS((int a));
int utf_toupper __ARGS((int a));
int utf_islower __ARGS((int a));
int utf_tolower __ARGS((int a));
int utf_isupper __ARGS((int a));
int mb_strnicmp __ARGS((char_u *s1, char_u *s2, int n));
void show_utf8 __ARGS((void));
int latin_head_off __ARGS((char_u *base, char_u *p));
int dbcs_head_off __ARGS((char_u *base, char_u *p));
int dbcs_screen_head_off __ARGS((char_u *base, char_u *p));
int utf_head_off __ARGS((char_u *base, char_u *p));
int mb_off_next __ARGS((char_u *base, char_u *p));
int mb_tail_off __ARGS((char_u *base, char_u *p));
int dbcs_screen_tail_off __ARGS((char_u *base, char_u *p));
void mb_adjust_cursor __ARGS((void));
void mb_adjustpos __ARGS((pos_T *lp));
char_u *mb_prevptr __ARGS((char_u *line, char_u *p));
int mb_charlen __ARGS((char_u *str));
int mb_dec __ARGS((pos_T *lp));
char_u *mb_unescape __ARGS((char_u **pp));
int mb_lefthalve __ARGS((int row, int col));
int mb_fix_col __ARGS((int col, int row));
char_u *enc_skip __ARGS((char_u *p));
char_u *enc_canonize __ARGS((char_u *enc));
char_u *enc_locale __ARGS((void));
int encname2codepage __ARGS((char_u *name));
void *my_iconv_open __ARGS((char_u *to, char_u *from));
int iconv_enabled __ARGS((int verbose));
void iconv_end __ARGS((void));
int im_xim_isvalid_imactivate __ARGS((void));
void im_set_active __ARGS((int active));
void xim_set_focus __ARGS((int focus));
void im_set_position __ARGS((int row, int col));
void xim_set_preedit __ARGS((void));
void xim_set_status_area __ARGS((void));
void xim_init __ARGS((void));
void xim_decide_input_style __ARGS((void));
int im_get_feedback_attr __ARGS((int col));
void xim_reset __ARGS((void));
int xim_queue_key_press_event __ARGS((GdkEventKey *event));
void xim_init __ARGS((void));
void im_shutdown __ARGS((void));
int xim_get_status_area_height __ARGS((void));
int im_get_status __ARGS((void));
int convert_setup __ARGS((vimconv_T *vcp, char_u *from, char_u *to));
int convert_input __ARGS((char_u *ptr, int len, int maxlen));
char_u *string_convert __ARGS((vimconv_T *vcp, char_u *ptr, int *lenp));
/* vim: set ft=c : */
