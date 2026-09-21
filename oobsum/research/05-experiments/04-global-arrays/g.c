int g_arr[10];
int gl_const(void)        { return g_arr[100]; }          /* global, constant idx  */
int gl_var(int i)         { return g_arr[i]; }            /* global, symbolic idx  */
int gl_call(void)         { return gl_var(100); }         /* global via callee     */
int lo_const(void)        { int a[10]; return a[100]; }   /* local,  constant idx  */
static int g_st[10];
int st_const(void)        { return g_st[100]; }           /* static global         */
int gl_ptr(void)          { int *p = g_arr; return p[100]; }
