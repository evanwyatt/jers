default: all

.DEFAULT:
	@cd src && $(MAKE) $@

install:
	@cd src && $(MAKE) $@

test:
	@cd tests && $(MAKE) $@
	@cd tests && ./run_tests

rpm:
	@./build/build.sh

.PHONY: install
