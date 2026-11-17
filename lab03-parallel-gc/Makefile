CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDLIBS  := -lpthread
TARGET  := pgc
SRC     := pgc.c

.PHONY: all run tsan helgrind clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

# Correccion concurrente: ThreadSanitizer no debe reportar data races.
# Grafo mas pequeno porque TSan instrumenta y ralentiza mucho.
tsan: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=thread -o $(TARGET)-tsan $< $(LDLIBS)
	./$(TARGET)-tsan 40000 32000

# Alternativa con helgrind (muy lento): grafo pequeno.
helgrind: $(TARGET)
	valgrind --tool=helgrind --error-exitcode=1 ./$(TARGET) 4000 3000

clean:
	rm -f $(TARGET) $(TARGET)-tsan
