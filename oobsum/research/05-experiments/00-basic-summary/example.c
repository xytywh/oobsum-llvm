int read_value(int *buf, int i) { return buf[i]; }

int wrapper(int *buf, int i) { return read_value(buf, i); }

int main() {
    int buf[10];
    return wrapper(buf, 20);
}
