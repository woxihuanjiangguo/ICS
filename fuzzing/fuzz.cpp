#include <cstdio>
#include <iostream>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <array>
#include <cstdlib>
#include <stdlib.h>
#include <curses.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <streambuf>
#include <sys/stat.h>
/*execute cmd*/
std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}
/*generate text files storing strings*/
void createTxt(const char *content, int number){
    std::ofstream outPut;
    outPut.open("./output/testCase"+std::to_string(number));
    if(outPut.is_open()){
        outPut << content << std::endl;
	outPut.close();
    }
}

void createInput(const char *fileName,const char *content){
    std::ofstream out;
    out.open(fileName);
    if(out.is_open()){
 	out << content;
        out.close();
    }
}

/*Ascii : 32-126 128-255*/
/*generate one single string*/
char * fuzz(char * precedingCase){
    /*String's length is 1200*/
    int length = 1200;
    /*random strategy: 1 or 2*/
    /*strategy 1 is pure randomization*/
    /*strategy 2 is based on the preceding case*/
    /*strategy 2 has 2 variations*/
    int strategy = 1;
    //int strategy = (rand() % (2))+ 1;
    char * result = (char *)calloc(length,4);
    if(strategy == 1){
        for(int i = 0;i<length;i++){
	    /*random 8 digits*/
	    unsigned int ascii = (rand() % (224))+ 32;
	    if(ascii == 127){
	        /*ascii 127 for DELETE*/
	        ascii++;
	    }
		result[i] = ascii;
    	}
    }else{
	/*2 variations*/
	/*operations on the preCase*/
	int variation = (rand() % (2))+ 1;
	for(int i = 0;i<length;i++){
	    result[i] = precedingCase[i];	
	}
	if(variation == 1){
	    /*2.1 a flip*/
	    int i = (rand()%(length));
	    int j = (rand()%(length));
	    if(i == j){
	    	i = 0;
  	    	j = 1;
	    }
	    char temp = result[i];
	    result[i] = result[j];
	    result[j] = temp;
	}else if(variation == 2){
	    /*2.2 1 readjustment: increment / decrement*/
	    int i = (rand()%(length));
	    unsigned int ascii = (rand() % (224))+ 32;
	    if(ascii == 127){
	        /*ascii 127 for DELETE*/
	        ascii++;
	    }
	    result[i] = ascii; 
	}
    }
    precedingCase = result;
    return result;
}


int main(int argNum,char *argv[]){
    std::string result;
    std::string result2;
    char command[] = " { ./base64_encode input ; } 2> error";
    char command2[] = "mkdir -p output ; rm -r output ; mkdir -p output";
    char text[] = "./input";
    const char * commandPointer = command;
    const char * commandPointer2 = command2;
    const char * contentPointer;
    const char * textPointer = text;
    char * preCasePointer;
    int testTimes = 1;
    int counter1 = 0;
    int counter2 = 0;
    int counter3 = 0;
    std::string pattern1 = "Abort";
    std::string pattern2 = "Segme";
    time_t start,end;
    
    start = time(NULL);
    //initialize the folder for txts
    result2 = exec(commandPointer2);
    while(1){
	end = time(NULL);
	contentPointer = fuzz(preCasePointer);
	createInput(textPointer,contentPointer);
	result = exec(commandPointer);
	if(result.size() == 0){
	    std::ifstream t("error");
	    std::string str((std::istreambuf_iterator<char>(t)),
            std::istreambuf_iterator<char>());
	    if(pattern1 == str.substr(0,5)){
		/*remove the extra information*/
		printf("\e[A1");
		printf("\e[K");
		counter3++;
	    }else if(pattern2 == str.substr(0,5)){
		counter2++;		
	    }
	    counter1++;
	    createTxt(contentPointer,counter1);
	}
	/*dump UI*/
	printf("Time taken: %f secs\n",difftime(start,end));
	printf("Total execs: %d\n",testTimes);
	printf("Total errors: %d\n",counter1);
	printf("Segementation faults: %d\n",counter2);
	printf("Stack smashing: %d\n",counter3);
	printf("\e[A1");
	printf("\e[K");
	printf("\e[A1");
	printf("\e[K");
	printf("\e[A1");
	printf("\e[K");
	printf("\e[A1");
	printf("\e[K");
	printf("\e[A1");
	printf("\e[K");

	testTimes++;
	usleep(10000);
    }
    return 0;	
}

