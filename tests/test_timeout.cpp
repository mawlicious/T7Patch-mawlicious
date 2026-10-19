#define MAWLICIOUS_TIMEOUT_TEST
#include "../LobbyTimeout.cpp"
#include <cassert>
#include <iostream>
void Setup(int value=10000, uint32_t hash=0xD23BBF56, uint32_t type=6) {
    lobby_timeout::Restore();
    testFound=true; testSupported=true; testWritable=true;
    std::memset(testDvar,0,sizeof(testDvar));
    std::memcpy(testDvar,&hash,4); std::memcpy(testDvar+0x1c,&type,4);
    std::memcpy(testDvar+0x28,&value,4);
}
int Current() { int n; std::memcpy(&n,testDvar+0x28,4); return n; }
bool Active() { return std::strstr(lobby_timeout::Label(),"NOT ACTIVE")==nullptr; }
int main() {
    Setup(); lobby_timeout::Apply(); assert(Current()==30000 && Active());
    lobby_timeout::Apply(); lobby_timeout::Restore(); assert(Current()==10000 && !Active());
    Setup(30000); lobby_timeout::Apply(); assert(Active()); lobby_timeout::Restore(); assert(Current()==30000);
    Setup(15000); lobby_timeout::Apply(); assert(Current()==15000 && !Active());
    Setup(10000,0x12345678); lobby_timeout::Apply(); assert(Current()==10000 && !Active());
    Setup(10000,0xD23BBF56,2); lobby_timeout::Apply(); assert(Current()==10000 && !Active());
    Setup(); testFound=false; lobby_timeout::Apply(); assert(!Active());
    Setup(); testSupported=false; lobby_timeout::Apply(); assert(!Active());
    Setup(); testWritable=false; lobby_timeout::Apply(); assert(Current()==10000 && !Active());
    Setup(); lobby_timeout::Apply(); int other=45000; std::memcpy(testDvar+0x28,&other,4);
    assert(!Active()); lobby_timeout::Restore(); assert(Current()==45000);
    std::cout << "Timeout setting, readback, guards, live label and restoration tests passed.\n";
}
