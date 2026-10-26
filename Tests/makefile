# makefile for all sample code

SUBDIRS = \
	libfeedkit \
	network

.PHONY: default clean test
	
default .DEFAULT :
	-@for f in $(SUBDIRS) ; do \
		$(MAKE) -C $$f -f makefile $@ || exit -1; \
	done

clean:
	-@for f in $(SUBDIRS) ; do \
		$(MAKE) -C $$f -f makefile clean || exit -1; \
	done

test: default
	LIBRARY_PATH="../build$${LIBRARY_PATH:+:$$LIBRARY_PATH}" \
		../build/Tests/feedkit_tests
	LIBRARY_PATH="../build$${LIBRARY_PATH:+:$$LIBRARY_PATH}" \
		../build/Tests/file_request_tests "$(shell pwd)/fixtures/integration.rss"
