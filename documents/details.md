# gcc 链接是会把没及时用的库舍弃……
- example:
链接所有库：必须先libplug.a+再libcom.a+ pthread
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	-pthread