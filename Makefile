# PKG Manager X Makefile
#
#   make                      PS5 payload  (pkgmgr.elf)
#   make PLATFORM=ps4         PS4 payload  (pkgmgr-ps4.elf, GoldHEN)
#   make HTTPS=0              build without mbedTLS (plain http:// sources only)

PYTHON := python3

PLATFORM ?= ps5
HTTPS    ?= 1

ifeq ($(PLATFORM),ps4)
SDK      := /opt/ps4-payload-sdk
TOOL     := $(SDK)/bin/orbis
PLATFORM_CFLAGS := -DPS4_BUILD
PLATFORM_LIBS   := -lSceNetCtl -lSceUserService -lSceSystemService -lSceAppInstUtil -lSceNet
LIBSMB2_LOCAL   := deps/libsmb2/build-ps4/lib/libsmb2.a
CMAKE_WRAPPER   := $(SDK)/bin/orbis-cmake
ELF             := pkgmgr-ps4.elf
else ifeq ($(PLATFORM),ps5)
SDK      := /opt/ps5-payload-sdk
TOOL     := $(SDK)/bin/prospero
PLATFORM_CFLAGS := -DPS5_BUILD
PLATFORM_LIBS   := -lSceNetCtl -lSceUserService -lSceSystemService -lSceAppInstUtil -lSceNet
LIBSMB2_LOCAL   := deps/libsmb2/build/lib/libsmb2.a
CMAKE_WRAPPER   := $(SDK)/bin/prospero-cmake
ELF             := pkgmgr.elf
else
$(error PLATFORM must be ps5 or ps4)
endif

CC     := $(TOOL)-clang
AR     := $(TOOL)-ar
RANLIB := $(TOOL)-ranlib
STRIP  := $(TOOL)-strip

TARGET   := $(SDK)/target
ifneq ($(wildcard $(TARGET)/lib/libsmb2.a),)
LIBSMB2  ?= $(TARGET)/lib/libsmb2.a
else
LIBSMB2  ?= $(LIBSMB2_LOCAL)
endif
INCLUDES := -Iinclude -I$(TARGET)/include -Ideps/libsmb2/include -Ideps/libsmb2/include/smb2

ifeq ($(HTTPS),1)
TLS_CFLAGS := -DPKGMGR_HAVE_TLS
TLS_LIBS   := $(TARGET)/lib/libmbedtls.a $(TARGET)/lib/libmbedx509.a $(TARGET)/lib/libmbedcrypto.a
endif

LIBS     := $(TARGET)/lib/libmicrohttpd.a \
            $(LIBSMB2) \
            $(TLS_LIBS) \
            -L$(TARGET)/lib -lpthread \
            $(PLATFORM_LIBS)

SRCS := src/main.c src/pkg_parser.c src/pkg_scanner.c src/pkg_cache.c src/smb_client.c src/smb_debug_log.c src/debug_log_retention.c src/installer.c \
        src/http_server.c src/stream_server.c src/stream_debug_log.c src/notification.c \
        src/multipart.c src/miniz.c src/app_info.c src/sqlite3.c src/icon_blurhash.c src/leftovers.c src/app_diag.c \
        src/app_installer.c
# Direct-install WebSocket upload modules.
SRCS_WS := src/ws_upload.c src/ws_stream.c
# PKG Manager X additions (HTTP sources, platform tags, console backends).
SRCS_X := src/pkg_platform.c src/pkg_parse_reader.c src/http_source.c src/http_sources_api.c \
          src/platform_install_ps5.c src/platform_install_ps4.c
OBJS := $(SRCS:.c=.o)

GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
GIT_DIRTY  := $(shell git diff --quiet 2>/dev/null || echo "-dirty")
BUILD_COMMIT ?= $(GIT_COMMIT)$(GIT_DIRTY)
BUILD_DATE   ?= $(shell date -u +"%Y-%m-%d_%H:%M:%S_UTC")

# PKG Manager X version, our own semver (include/version_x.h). Releases pass
# X_VERSION from the x-v<version> tag; otherwise `git describe` (e.g.
# 1.1.0-3-gabc1234) or 0.0.0-dev. The "based on" version is upstream's
# PKGMGR_VERSION from include/version.h.
X_VERSION ?= $(shell bash tools/fork_version.sh dev 2>/dev/null)
ifneq ($(X_VERSION),)
X_VERSION_CFLAGS := -DPKGMGR_X_VERSION=\"$(X_VERSION)\"
endif

FRONTEND_DIST := frontend/dist/index.html
ASSET_HEADER  := include/assets_index_html.h
MANIFEST_DIST := frontend/dist/cache.appcache
MANIFEST_HEADER := include/assets_cache_appcache.h
FAVICON_SVG_DIST   := frontend/dist/favicon.svg
FAVICON_SVG_HEADER := include/assets_favicon_svg.h
ICON_PNG_DIST      := frontend/dist/icon.png
ICON_PNG_HEADER    := include/assets_icon_png.h
PARAM_JSON_DIST    := assets/param.json
PARAM_JSON_HEADER  := include/assets_param_json.h
ICON0_PNG_DIST     := assets/icon0.png
ICON0_PNG_HEADER   := include/assets_icon0_png.h
ASSET_HEADERS      := $(ASSET_HEADER) $(MANIFEST_HEADER) $(FAVICON_SVG_HEADER) $(ICON_PNG_HEADER) $(PARAM_JSON_HEADER) $(ICON0_PNG_HEADER)

CFLAGS := -Os -Wall -Wno-visibility $(PLATFORM_CFLAGS) $(TLS_CFLAGS) $(X_VERSION_CFLAGS) -D_BSD_SOURCE -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=2 -DSQLITE_OMIT_WAL -DPKGMGR_BUILD_COMMIT=\"$(BUILD_COMMIT)\" -DPKGMGR_BUILD_DATE=\"$(BUILD_DATE)\" -ffunction-sections -fdata-sections $(INCLUDES)
LDFLAGS := -Wl,--gc-sections

# Host test build (uses tests/mock_smb.c instead of real libsmb2; MHD not needed)
TEST_CFLAGS := -g -O0 -Wall -Wextra -D_GNU_SOURCE -Iinclude -Ideps/libsmb2/include -Ideps/libsmb2/include/smb2 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=2 -DSQLITE_OMIT_WAL -DPKGMGR_BUILD_COMMIT=\"$(BUILD_COMMIT)\" -DPKGMGR_BUILD_DATE=\"$(BUILD_DATE)\"
TEST_SRCS := src/multipart.c src/pkg_parser.c src/pkg_scanner.c src/pkg_cache.c src/miniz.c src/smb_client.c src/smb_debug_log.c src/debug_log_retention.c src/installer.c src/stream_server.c src/stream_debug_log.c src/notification.c src/app_info.c src/icon_blurhash.c src/leftovers.c src/app_diag.c src/app_installer.c src/sqlite3.c tests/mock_smb.c tests/ps5_sim.c src/ws_upload.c src/ws_stream.c tests/ws_test_client.c \
             src/pkg_platform.c src/pkg_parse_reader.c src/http_source.c tests/http_test_server.c
TESTS := test_pkg_parser test_pkg_scanner test_pkg_cache test_installer test_leftovers test_edge_cases test_multipart test_stream_sim test_ws_upload test_direct_install_e2e test_ws_stream test_ws_stream_far test_parse_mem \
         test_pkg_platform test_http_source

all: $(ELF)

.PHONY: frontend-build
frontend-build:
	@echo "Building frontend..."
	@VERSION="$(X_VERSION)"; [ -n "$$VERSION" ] || VERSION=0.0.0-dev; \
	UPSTREAM=$$(bash tools/fork_version.sh upstream); \
	COMMIT=$$(git rev-parse --short HEAD 2>/dev/null || echo "unknown"); \
	DATE=$$(date -u +"%Y-%m-%d %H:%M:%S UTC"); \
	TITLE="PKG Manager X v$$VERSION ($$COMMIT, $$DATE) - based on PKG Manager v$$UPSTREAM by PLK"; \
	echo "Updating title in index.html to: $$TITLE"; \
	(cd frontend && npm ci && VITE_APP_VERSION="$$VERSION" VITE_APP_UPSTREAM_VERSION="$$UPSTREAM" VITE_APP_COMMIT="$$COMMIT" VITE_APP_BUILD_DATE="$$DATE" npm run build); \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/pkgmgr.XXXXXX"); \
	sed -e "s|\[\[TITLE_PLACEHOLDER\]\]|$$TITLE|g" -e "s|<title>.*</title>|<title>$$TITLE</title>|g" frontend/dist/index.html > $$TMP; \
	mv $$TMP frontend/dist/index.html; \
	chmod 644 frontend/dist/index.html; \
	echo "Updating build date in cache.appcache to: $$DATE"; \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/pkgmgr.XXXXXX"); \
	sed "s|\[\[BUILD_DATE\]\]|$$DATE|g" frontend/dist/cache.appcache > $$TMP; \
	mv $$TMP frontend/dist/cache.appcache; \
	chmod 644 frontend/dist/cache.appcache
	@echo "Rewriting modern rgb() slash syntax for Safari 12..."
	$(PYTHON) tools/fix_legacy_css.py $(FRONTEND_DIST)
	@echo "Generating asset headers..."
	$(PYTHON) tools/gen_assets.py $(FRONTEND_DIST) $(ASSET_HEADER) index_html
	$(PYTHON) tools/gen_assets.py $(MANIFEST_DIST) $(MANIFEST_HEADER) cache_appcache
	$(PYTHON) tools/gen_assets.py $(FAVICON_SVG_DIST) $(FAVICON_SVG_HEADER) favicon_svg
	$(PYTHON) tools/gen_assets.py $(ICON_PNG_DIST) $(ICON_PNG_HEADER) icon_png
	$(PYTHON) tools/gen_assets.py $(PARAM_JSON_DIST) $(PARAM_JSON_HEADER) param_json
	$(PYTHON) tools/gen_assets.py $(ICON0_PNG_DIST) $(ICON0_PNG_HEADER) icon0_png

$(ASSET_HEADER): $(FRONTEND_DIST)
	$(PYTHON) tools/gen_assets.py $(FRONTEND_DIST) $(ASSET_HEADER) index_html

$(MANIFEST_HEADER): $(FRONTEND_DIST)
	$(PYTHON) tools/gen_assets.py $(MANIFEST_DIST) $(MANIFEST_HEADER) cache_appcache

$(FAVICON_SVG_HEADER): $(FAVICON_SVG_DIST)
	$(PYTHON) tools/gen_assets.py $(FAVICON_SVG_DIST) $(FAVICON_SVG_HEADER) favicon_svg

$(ICON_PNG_HEADER): $(ICON_PNG_DIST)
	$(PYTHON) tools/gen_assets.py $(ICON_PNG_DIST) $(ICON_PNG_HEADER) icon_png

$(PARAM_JSON_HEADER): $(PARAM_JSON_DIST)
	$(PYTHON) tools/gen_assets.py $(PARAM_JSON_DIST) $(PARAM_JSON_HEADER) param_json

$(ICON0_PNG_HEADER): $(ICON0_PNG_DIST)
	$(PYTHON) tools/gen_assets.py $(ICON0_PNG_DIST) $(ICON0_PNG_HEADER) icon0_png

$(FRONTEND_DIST):
	@echo "ERROR: frontend/dist/index.html not found! Run 'make frontend-build' first."
	@exit 1

$(LIBSMB2_LOCAL):
	@echo "Building libsmb2 for $(PLATFORM)..."
	mkdir -p $(dir $(LIBSMB2_LOCAL))/.. && cd $(dir $(LIBSMB2_LOCAL))/.. && \
	$(CMAKE_WRAPPER) $(CURDIR)/deps/libsmb2 -DBUILD_SHARED_LIBS=OFF && \
	$(MAKE) -j$$(nproc)

$(ELF): $(ASSET_HEADERS) $(LIBSMB2) $(SRCS) $(SRCS_WS) $(SRCS_X)
	@echo "Building $(ELF) for $(PLATFORM) (HTTPS=$(HTTPS))..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(ELF) $(SRCS) $(SRCS_WS) $(SRCS_X) $(LIBS)
	@echo "Stripping $(ELF)..."
	$(STRIP) $(ELF)

clean:
	rm -f pkgmgr.elf pkgmgr-ps4.elf pkgmgr_v*.elf pkg-manager_v*.elf pkg-manager-x_v*.elf $(ASSET_HEADERS) src/*.o $(addprefix tests/,$(TESTS))
	rm -rf $(addprefix tests/,$(addsuffix .dSYM,$(TESTS)))

test: $(PARAM_JSON_HEADER) $(ICON0_PNG_HEADER)
	@for t in $(TESTS); do \
		echo "=== CC tests/$$t ==="; \
		cc $(TEST_CFLAGS) -o tests/$$t tests/$$t.c $(TEST_SRCS) -lpthread -lm -ldl || exit 1; \
	done
	@echo "=== RUN tests (emulating PS4 console) ==="
	PKGMGR_CONSOLE=ps4 ./tests/test_pkg_platform --ps4 || exit 1
	@for t in $(TESTS); do \
		echo "=== RUN tests/$$t ==="; \
		./tests/$$t || exit 1; \
	done
	@echo "=== RUN tests/test_fix_legacy_css.py ==="
	$(PYTHON) tests/test_fix_legacy_css.py

dist-clean: clean
	rm -rf frontend/dist frontend/node_modules

.PHONY: all clean test frontend-build dist-clean mock ps5 ps4
mock:
	node frontend/mock-server.js

# Convenience wrappers (each console builds inside its own SDK image).
ps5:
	$(MAKE) PLATFORM=ps5
ps4:
	$(MAKE) PLATFORM=ps4
