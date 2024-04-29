TARGET	= hcx
OUT		= build
CFLAGS	+= -Wall \
		   -Wextra \
		   -Wpedantic \
		   -Wno-unknown-warning-option \
		   -Wno-format-truncation

PREFIX	= /usr/local
BINDIR	= $(PREFIX)/bin

SRC	= $(wildcard src/*.c)
OBJ	= $(OUT)/main.o
DEP	= $(OBJ:.o=.d)

release: CFLAGS += -O2 -DNDEBUG
release: build

debug: CFLAGS += -g -D_FORTIFY_SOURCE=1
debug: build

build: $(OUT)/$(TARGET)

$(OUT)/$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(OUT)/%.o: src/%.c
	mkdir -p "$(OUT)"
	$(CC) $(CFLAGS) -std=c99 -MMD -c $< -o $@

clean:
	$(RM) -r $(OBJ) $(DEP) "$(OUT)/$(TARGET)"
	$(RM) -r "example/public"

install:
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 "$(OUT)/$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"
	install -m 755 "dist/hc" "$(DESTDIR)$(BINDIR)/hc"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	$(RM) "$(DESTDIR)$(BINDIR)/hc"

run: debug
	"./$(OUT)/$(TARGET)"

test: CFLAGS += -DTEST
test: run

example: debug
	cd example && "../$(OUT)/$(TARGET)"

format:
	clang-format -i $(SRC)

check:
	cppcheck \
		--language=c \
		--std=c99 \
		--enable=all \
		--check-level=exhaustive \
		--inconclusive \
		--quiet \
		--suppress=unmatchedSuppression \
		--suppress=missingIncludeSystem \
		--suppress=constParameterPointer \
		--suppress=constVariablePointer \
		src

	clang-tidy $(SRC)
	codespell \
		src \
		dist \
		Makefile \
		README \
		LICENSE

iwyu:
	include-what-you-use $(SRC)

valgrind: debug
	cd example && valgrind \
		--leak-check=full \
		--show-leak-kinds=all \
		--track-origins=yes \
		--verbose \
		"../$(OUT)/$(TARGET)"

-include $(DEP)

.PHONY: release debug
.PHONY: build clean
.PHONY: install uninstall
.PHONY: run test example
.PHONY: format check iwyu valgrind
