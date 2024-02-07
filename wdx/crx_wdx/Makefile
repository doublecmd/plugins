#############################################################################
# Makefile for building: crx_wdx.wdx
#############################################################################

all:
	cargo build --release
	mv target/release/libcrx_wdx.so crx_wdx.wdx
	strip crx_wdx.wdx

clean:
	rm -rf target
	rm -f crx_wdx.wdx
