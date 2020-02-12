MODULES = pg_json_decoding
PG_CONFIG = pg_config
PG_CPPFLAGS = -Werror
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
