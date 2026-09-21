int f(int k) {
    int a[10];
    int i;
    switch (k) {
      case 0: i = 1; break;
      case 1: i = 5; break;
      case 2: i = 14; break;        /* OOB */
      default: i = 0;
    }
    return a[i];
}
