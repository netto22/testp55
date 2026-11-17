CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDLIBS  := -lpthread
TARGET  := conc
SRC     := conc.c

.PHONY: all run stress tsan helgrind clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

# Carga asimetrica: pocos productores, muchos consumidores y viceversa.
stress: $(TARGET)
	./$(TARGET) 1 8 200000
	./$(TARGET) 8 1 50000
	./$(TARGET) 8 8 50000

# Verificacion dinamica: ThreadSanitizer no debe reportar data races.
tsan: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=thread -o $(TARGET)-tsan $< $(LDLIBS)
	./$(TARGET)-tsan 4 4 50000

# Alternativa: helgrind (si valgrind esta instalado). Mas lento; menos items.
helgrind: $(TARGET)
	valgrind --tool=helgrind --error-exitcode=1 ./$(TARGET) 2 2 2000

clean:
	rm -f $(TARGET) $(TARGET)-tsan
