TARGET	= hcx
OUT		= build
CFLAGS	+= -Wall \
		   -Wextra \
		   -Wpedantic \
		   -Wno-unknown-warning-option \
		   -Wno-format-truncation

PREFIX	= /usr/local
BINDIR	= $(PREFIX)/bin

OBJS	= $(OUT)/main.o
DEPS	= $(OBJS:.o=.d)

release: CFLAGS += -O2 -DNDEBUG
release: build

debug: CFLAGS += -g -D_FORTIFY_SOURCE=1
debug: build

build: $(OUT)/$(TARGET)

$(OUT)/$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OUT)/%.o: src/%.c
	mkdir -p "$(OUT)"
	$(CC) $(CFLAGS) -std=c99 -MMD -c $< -o $@

clean:
	rm -rf $(OBJS) $(DEPS) "$(OUT)/$(TARGET)"
	rm -rf "example/public"

run: debug
	"./$(OUT)/$(TARGET)"

test: CFLAGS += -DTEST
test: run

example: debug
	cd example && "../$(OUT)/$(TARGET)"

install:
	mkdir -p "$(DESTDIR)$(BINDIR)"
	install -m755 "$(OUT)/$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"
	install -m755 "dist/hc" "$(DESTDIR)$(BINDIR)/hc"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(BINDIR)/hc"

format:
	-clang-format -i src/*.c

check:
	-codespell \
		--skip="*.html"

	-clang-tidy src/*.c

	-cppcheck \
		--std=c99 \
		--enable=all \
		--suppress=unmatchedSuppression \
		--suppress=missingIncludeSystem \
		--suppress=constParameterPointer \
		--suppress=constVariablePointer \
		--check-level=exhaustive \
		--inconclusive \
		--quiet \
		src

valgrind: debug
	-cd example && valgrind \
		--leak-check=full \
		--show-leak-kinds=all \
		--track-origins=yes \
		--verbose \
		"../$(OUT)/$(TARGET)"

-include $(DEPS)

.PHONY: release debug
.PHONY: build clean
.PHONY: run test example
.PHONY: install uninstall
.PHONY: format check valgrind
