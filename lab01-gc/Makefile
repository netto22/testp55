CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
TARGET  := gc
SRC     := gc.c

.PHONY: all run asan valgrind clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

# Verificacion dinamica: AddressSanitizer debe reportar 0 errores y 0 leaks.
asan: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $(TARGET)-asan $<
	./$(TARGET)-asan

# Alternativa con valgrind si esta instalado.
valgrind: $(TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET)-asan
