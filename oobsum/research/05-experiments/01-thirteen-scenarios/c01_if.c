int f(int flag) {
    int a[10];
    int i = 0;
    if (flag) i = 12;            /* i in {0,12}, 12 >= 10 */
    return a[i];
}
