include /usr/local/armdsp/Makefile.armdsp
ARMDSP_NFSROOT=/arm

all: app.dsp

APP_OBJS = app.obj
app.dsp: $(APP_OBJS)
	$(ARMDSP_LINK) -o app.dsp $(APP_OBJS)

test: all
	install -c -m 644 app.dsp $(ARMDSP_NFSROOT)/.

clean:
	rm -f *~ *.obj *.dsp *.elf
