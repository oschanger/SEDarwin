POLICY=		stub
POLICY_VER=	1.0
POLICY_COMPVER=	1.0
POLICY_DESC=	"MAC Stub Policy"
POLICY_SRCS=	mac_stub.c
POLICY_MAN=	mac_stub.4

include ../Makeconfig
include $(DARWIN_ROOT)/build/PolicyKext.mk

CLEANFILES+=	stub_funcs.h stub_policy_ops.h policy.in

mac_stub.c: stub_funcs.h stub_policy_ops.h

policy.in: $(EXPORT_HDRS)/bsd/sys/mac_policy.h
	@cpp -P $< \
		| grep -v ^\$ \
		| awk 'RS=";" { if ($$1 == "typedef") { print $$0";" } }' \
		| tr -d "\n\t" \
		| tr ";" "\n" \
		| sed	-e 's/typedef //g' \
			-e 's/,/, /g' \
			-e 's/_t(/ (/g' \
			-e 's/ mpo_/ /g' \
		> $@

stub_funcs.h: policy.in
	@cat $< \
		| awk -f mk_stub_funcs.awk \
		> $@

stub_policy_ops.h: policy.in
	@cat $< \
		| awk -f mk_stub_policy_ops.awk \
		> $@
