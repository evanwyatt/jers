default: all

.DEFAULT:
	@cd src && $(MAKE) $@

install:
	@cd src && $(MAKE) $@

test:
	@cd tests && $(MAKE) $@
	@cd tests && ./run_tests

.PHONY: install
