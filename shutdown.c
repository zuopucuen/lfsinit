#include <signal.h>
#include <unistd.h>

int main(){
	kill(1, SIGUSR2);
}
