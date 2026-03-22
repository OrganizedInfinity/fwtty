.PHONY: all clean run debug
.DEFAULT_GOAL = run

TARGET_DIR := build
TARGET := $(TARGET_DIR)/fwtty

OBJECTS := opts

$(TARGET_DIR):
	mkdir $@

OBJECTS := $(addprefix $(TARGET_DIR)/,$(OBJECTS))
OBJECTS := $(addsuffix .o,$(OBJECTS))
$(OBJECTS): $(TARGET_DIR)/%.o: src/%.c src/%.h $(TARGET_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): src/main.c $(OBJECTS) $(TARGET_DIR)
	$(CC) $(CFLAGS) $< $(OBJECTS) -lpthread -o $@

all: $(TARGET)

debug: CFLAGS += -g
debug: all

run: all
	./$(TARGET)

clean:
	rm -rf $(TARGET_DIR)
