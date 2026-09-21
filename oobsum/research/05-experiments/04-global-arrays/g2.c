int g_noinit[10];                 /* tentative definition, no initializer      */
int g_init[10] = {1,2,3,4,5,6,7,8,9,10};   /* real definition with initializer */
static int g_st_init[10] = {0};
int f_noinit(void)   { return g_noinit[100]; }
int f_init(void)     { return g_init[100]; }
int f_st_init(void)  { return g_st_init[100]; }
