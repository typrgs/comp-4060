TARGETS = drivers app
TYPE = debug

.PHONY: targets $(TARGETS)

targets: $(TARGETS)

debug: TYPE = debug
debug: $(TARGETS)
release: TYPE = release
release: $(TARGETS)

$(TARGETS):
	@$(MAKE) $(TYPE) -C $@

%-install: %/main.elf
	@$(MAKE) -C $* main-install

.PHONY: clean
clean: 
	@for q in $(TARGETS); do \
		make -C $$q clean; \
	done
