include $(APPDIR)/Make.defs
#include libsioclient/Make.defs

# Stella

CXXSRCS += socket_reader.cxx #sd_controller.cxx #data_handler.cxx
MAINSRC = user_src_main.cxx 

# Stella built-in application info

PROGNAME = user_src
PRIORITY = 100
STACKSIZE = 2048
MODULE = $(CONFIG_STELLA_APP)
#MODULE = m

#ARCHXXINCLUDES += ${shell $(INCDIR) -s "$(CC)" $(APPDIR)$(DELIM)user_src$(DELIM)libsioclient$(DELIM)lib$(DELIM)asio$(DELIM)asio$(DELIM)include}
#ARCHXXINCLUDES += ${shell $(INCDIR) -s "$(CC)" $(APPDIR)$(DELIM)user_src$(DELIM)libsioclient$(DELIM)lib$(DELIM)websocketpp}
#ARCHXXINCLUDES += ${shell $(INCDIR) -s "$(CC)" $(APPDIR)$(DELIM)user_src$(DELIM)libsioclient$(DELIM)lib$(DELIM)rapidjson$(DELIM)include}
#ARCHXXINCLUDES += ${shell $(INCDIR) -s "$(CC)" $(APPDIR)$(DELIM)user_src$(DELIM)libs$(DELIM)include$(DELIM)}

#CXXFLAGS += -DBOOST_DATE_TIME_NO_LIB -DBOOST_REGEX_NO_LIB -DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STL_ -D_WEBSOCKETPP_CPP11_FUNCTIONAL_ -D_DEBUG=0 -DSIO_TLS=0 -DSIOCATMARK -fexceptions -frtti -lcxxabi

include $(APPDIR)/Application.mk
