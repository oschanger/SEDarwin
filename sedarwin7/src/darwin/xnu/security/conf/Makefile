export MakeInc_cmd=${SRCROOT}/makedefs/MakeInc.cmd
export MakeInc_def=${SRCROOT}/makedefs/MakeInc.def
export MakeInc_rule=${SRCROOT}/makedefs/MakeInc.rule
export MakeInc_dir=${SRCROOT}/makedefs/MakeInc.dir


include $(MakeInc_cmd)
include $(MakeInc_def)

SETUP_SUBDIRS = \
	tools

COMP_SUBDIRS = 

INST_SUBDIRS = 

ifndef SECURITY_KERNEL_CONFIG
export SECURITY_KERNEL_CONFIG = $(KERNEL_CONFIG)
endif

COMPOBJROOT=$(OBJROOT)/$(KERNEL_CONFIG)_$(ARCH_CONFIG)/$(COMPONENT)

$(OBJROOT)/$(KERNEL_CONFIG)_$(ARCH_CONFIG)/$(COMPONENT)/doconf:
	make build_setup 

$(COMPOBJROOT)/$(SECURITY_KERNEL_CONFIG)/Makefile :  $(SOURCE)/MASTER  \
	$(SOURCE)/MASTER.$(ARCH_CONFIG_LC) \
	$(SOURCE)/Makefile.template  \
	$(SOURCE)/Makefile.$(ARCH_CONFIG_LC)  \
	$(SOURCE)/files \
	$(SOURCE)/files.$(ARCH_CONFIG_LC) \
	$(OBJROOT)/$(KERNEL_CONFIG)_$(ARCH_CONFIG)/$(COMPONENT)/doconf
	@echo "Running doconf for $(SECURITY_KERNEL_CONFIG)";
	(doconf_target=$(addsuffix /conf, $(TARGET)); \
	echo $${doconf_target};\
	$(MKDIR) $${doconf_target}; \
	cd $${doconf_target}; \
	rm -f $(notdir $?); \
	cp  $? $${doconf_target}; \
	$(OBJROOT)/$(KERNEL_CONFIG)_$(ARCH_CONFIG)/$(COMPONENT)/doconf -c -cpu $(ARCH_CONFIG_LC) -d  $(TARGET)/$(SECURITY_KERNEL_CONFIG) $(SECURITY_KERNEL_CONFIG); \
	);

.ORDER: $(COMPOBJROOT)/$(SECURITY_KERNEL_CONFIG)/Makefile

do_setup_conf: $(OBJROOT)/$(KERNEL_CONFIG)_$(ARCH_CONFIG)/$(COMPONENT)/doconf \
		$(COMPOBJROOT)/$(SECURITY_KERNEL_CONFIG)/Makefile

do_all: do_setup_conf
	@echo "[ $(SOURCE) ] Starting do_all  $(COMPONENT) $(SECURITY_KERNEL_CONFIG) $(ARCH_CONFIG) $(TARGET)";	\
	next_source=$(subst conf/,,$(SOURCE));			\
	${MAKE} -C $(COMPOBJROOT)/$(SECURITY_KERNEL_CONFIG)	\
		MAKEFILES=$(TARGET)/$(SECURITY_KERNEL_CONFIG)/Makefile	\
		SOURCE=$${next_source}			\
		TARGET=$(TARGET)					\
		INCL_MAKEDEP=FALSE	\
		build_all;	\
	echo "[ $(SOURCE) ] Returning do_all $(COMPONENT) $(SECURITY_KERNEL_CONFIG) $(ARCH_CONFIG) $(TARGET)";

do_build_all: do_all 

include $(MakeInc_rule)
include $(MakeInc_dir)

