# Build Magic Tattoos UI.
# make staging            copies into the Vortex mod folder
# make live               copies straight into the Skyrim Data folder
# make package            writes dist/MagicTattoosUI-<version>.zip for Nexus
# make test               builds and runs the offline tests only
# make uninstall-staging  deletes the Vortex mod folder
# make uninstall-live     deletes the plugin from the Skyrim Data folder
SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
.PHONY: all staging live package test uninstall-staging uninstall-live

all: staging

staging:
	bash build.sh staging

live:
	bash build.sh live

package:
	bash build.sh package

test:
	bash build.sh test

uninstall-staging:
	bash build.sh uninstall-staging

uninstall-live:
	bash build.sh uninstall-live
