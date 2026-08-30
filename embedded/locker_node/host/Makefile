# Host build of the locker node. Same domain sources as the board.
SRC = ../src
CRY = ../src/crypto
# Dead-strip like the board build does (-ffunction-sections + gc-sections).
# Without it the linker keeps the key-generation paths nobody calls and the
# deliberately-undefined RNG symbol fails the link — which is the point of
# that declaration, just fired at the wrong build.
CFLAGS  = -ffunction-sections -fdata-sections -std=c11 -Wall -Wextra -I$(SRC) -I$(CRY) -I$(CRY)/utility
CXXFLAGS = -ffunction-sections -fdata-sections -std=c++11 -Wall -I$(SRC) -I$(CRY) -I$(CRY)/utility

# One domain per binary — a node links exactly one, same as a target does.
DOMAIN ?= payment_locker
DOM = ../domains
C_SRCS  = host_main.c $(DOM)/$(DOMAIN).c $(SRC)/authority.c $(SRC)/mcp_server.c \
          $(SRC)/mcp_stream_serve.c $(SRC)/mcp_writer.c $(SRC)/json_min.c \
          $(SRC)/line_reader.c
CXX_SRCS = $(SRC)/ed25519_shim.cpp $(CRY)/Ed25519.cpp $(CRY)/Curve25519.cpp \
           $(CRY)/Crypto.cpp $(CRY)/SHA512.cpp $(CRY)/Hash.cpp \
           $(CRY)/BigNumberUtil.cpp

$(DOMAIN)_host: $(C_SRCS) $(CXX_SRCS)
	$(CXX) $(CXXFLAGS) -c $(CXX_SRCS)
	$(CC) $(CFLAGS) -c $(C_SRCS)
	$(CXX) -Wl,-dead_strip -o $(DOMAIN)_host *.o
	rm -f *.o

clean:
	rm -f *_host *.o
