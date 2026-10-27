# Optional clients are restored in independently validated groups. Keep the
# historical clients out of the default client build until each one compiles,
# launches, and has a bounded smoke test.

SUPPORTED_SUBDIRS = \
	RegisterFeed \
	FeedSubscriber

.PHONY: default clean

default:
	@for directory in $(SUPPORTED_SUBDIRS); do \
		$(MAKE) -C $$directory -f makefile default || exit 1; \
	done

clean:
	@for directory in $(SUPPORTED_SUBDIRS); do \
		$(MAKE) -C $$directory -f makefile clean || exit 1; \
	done
