//HERE BE DRAGONS
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>

struct timeval _randSeed , _tBegin, _tNow;
struct sizeOfMatrix {int _sizeOfArray;};
volatile sig_atomic_t _NewPath = 0;

//This function read the contents of the file path provided and stores them in a matrix
int **readFile(char* _nameFile, int **_fileMatrix, int *_size)
{
    FILE *_file;
    int _sizeOfMatrix;

	_file = fopen(_nameFile, "r");

	if(_file == NULL)
	{
		return 0;
	}

    fscanf(_file, "%d", &_sizeOfMatrix);

    int i, j, num;
    _fileMatrix = (int **)malloc( _sizeOfMatrix * sizeof(int *) );

    for(i = 0 ; i < _sizeOfMatrix ; i++ )
    {
        _fileMatrix[i] = (int *)malloc( _sizeOfMatrix * sizeof(int) );
        
        for(j = 0 ; j < _sizeOfMatrix ; j++ )
        {
            fscanf(_file, "%d", &num);
            _fileMatrix[i][j] = num;
        }
    }

    if(_file!=NULL) fclose(_file);
    *_size = _sizeOfMatrix;
    return _fileMatrix;
}

//Randomizes the Path
int *randomizePath(int _numShuffles, int _pathIterationSize, int *_pathPointer)
{
    int i, _random1, _random2, _aux1, _aux2;
    for (i = 0 ; i < _numShuffles ; i++)
    {
        _random1 = rand() % _pathIterationSize;
        _random2 = rand() % _pathIterationSize;
        while(_random1 == _random2)
        {
            _random1 = rand() % _pathIterationSize;
            _random2 = rand() % _pathIterationSize;
        }
        
        _aux1 = _pathPointer[_random1];
        _aux2 = _pathPointer[_random2];
        
        _pathPointer[_random1] = _aux2;
        _pathPointer[_random2] = _aux1;
    }
    return _pathPointer;
}

//This function initiates and shuffles the path
int *createPath(int _pathSize)
{
    int i;
    int *_path = malloc(_pathSize * sizeof(int));
    for (i = 0 ; i < _pathSize ; i++)
    {
        _path[i] = (1+i);
    }
    _path = randomizePath(_pathSize, _pathSize, _path);
    return _path;
}

//Calculates the best distance
int calculateDistance(int *_pathToRun, int _sizeOfPath, int _bestDistCurrent, int iterationCounter, sem_t writeMutex, double *shmem, int *shmemPath, int **_fileMatrix)
{
	int i, x, y, _currentDist = 0;
	
	for(i = 0; i < _sizeOfPath ; i++)
	{
		if(i != _sizeOfPath-1)
		{
			y= _pathToRun[i+1]-1;
		}
		else
		{
			y = _pathToRun[0]-1;
		}
		
		x = _pathToRun[i]-1;
		
		_currentDist+= _fileMatrix[x][y];
	}

	if(_bestDistCurrent > _currentDist || _bestDistCurrent == 0)
	{
		_bestDistCurrent = _currentDist;
        gettimeofday(&_tNow, NULL);

        sem_wait(&writeMutex);
        if(shmem[2] > _bestDistCurrent || shmem[2]  == 0)
        {
            //comp_res OR Size of path
            shmem[2] = _bestDistCurrent;
            
            //niter_res OR Number of iterations untill result
            shmem[3] = iterationCounter;

            //TODO: tempo_res or time left before end of program
            double _timeToEnd = ( _tNow.tv_sec - _tBegin.tv_sec);
            _timeToEnd += (_tNow.tv_usec / 1000.0)/1000;
            
            //printf("T[%f]",_timeToEnd);
            
            shmem[4] = _timeToEnd;
            
            //getPid for easy identification and debug
            for(i=0 ; i < _sizeOfPath ; i++)
            {
                shmemPath[i] = _pathToRun[i];
            }
            shmem[7] = getpid();
            kill(getppid(), SIGUSR1);
            //printf("C[%d]|D[%d],T[%f],I=[%d]\n", getpid(), _bestDistCurrent,_timeToEnd, iterationCounter);
        }
        else
        {
            _bestDistCurrent = *shmem;
        }
        sem_post(&writeMutex);
	}
	
	return _bestDistCurrent;
}

//The Old Gods of demand that the memory be free
void freeMemory(int _size, int **_fileMatrix)
{
    int i;

    for (i = 0 ; i < _size ; i++)
    {
        free(_fileMatrix[i]);
    }
    free(_fileMatrix);
}

//Create all processes
int *createWorkers(int _num_workers, int _size, sem_t writeMutex, double* shmem, int* shmemPath, int **_fileMatrix)
{
    int *_workersPid = malloc(_num_workers * sizeof(int));
    int i, _bestDist = 0;
    // Fork worker processes
    for (i=0; i<_num_workers; i++) {
        _workersPid[i] = fork();
        if(_workersPid[i] == 0)
        {
            gettimeofday(&_randSeed, NULL);
            srand(_randSeed.tv_sec*getpid());
            
            gettimeofday(&_tNow, NULL);
            int iterationCounter = 0;
            int *_realPath = createPath(_size);
            while(1){
                if(_NewPath)
                {
                    _NewPath = 0;
                    //printf("\nCHILD[%d] Got Path[", getpid());
                    for(i=0 ; i<_size ; i++)
                    {
                        _realPath[i]=shmemPath[i];
                        //printf("%d,",_realPath[i]);
                    }
                    //printf("\b]\n");
                }
                _bestDist = calculateDistance(_realPath, _size, _bestDist, iterationCounter, writeMutex, shmem, shmemPath, _fileMatrix);
                _realPath = randomizePath(1, _size, _realPath);
                iterationCounter++;
            }
        }
    }
    return _workersPid;
}

//Kills all processes
void killWorkers(int *_workerArray, int _num_workers)
{
    for (int i=0; i<_num_workers; i++) {
        //printf("Killing %d\n", _workerArray[i]);
        kill(_workerArray[i], SIGKILL);
    }
}

//Handles wait for it... signals (shocking i know)
void signalHandler(int signum)
{
    if (signum == SIGUSR1)
    {
        //perror("SIGNAl");
        //Thanks to arcane workings (or a well made lenguage)
        //Every fork child belongs to the same group
        killpg(getpgrp(), SIGUSR2);
    }
    else if (signum == SIGUSR2)
    {
        _NewPath = 1;
    }
}


//Main
int main(int argc, char* argv[])
{
    //Initiate Synch Tools
    sem_t writeMutex;
    sem_init(&writeMutex, 0, 1);
    int *shmemPath;
    double *shmem;
    signal(SIGUSR1, signalHandler);
    signal(SIGUSR2, signalHandler);
    //SIGUSR2 MUST BE INSTALLED HERE BECUASE OF ASYNCH BEHAVIOUR

    //Initiate Timers and Random
    gettimeofday(&_tBegin, NULL);

    int i, j;
    int **_fileMatrix;
    int protection = PROT_READ | PROT_WRITE;
    int visibility = MAP_ANONYMOUS | MAP_SHARED;
    int _size;

    if(atoi(argv[4]) > 1)
    {
        int _numberOfTests;
        double m_iterations = 0, m_time = 0;

        for(_numberOfTests=0; _numberOfTests < atoi(argv[4]); _numberOfTests++)
        {
            _fileMatrix = readFile(argv[1], _fileMatrix, &_size);
            
            int _num_workers = atoi(argv[2]);

            // -1 is POSIX standard to run the shmem in memory instead of file
            shmem = mmap(NULL, _num_workers*(sizeof(int)), protection, visibility, -1, 0);
            shmemPath = mmap(NULL, _size*sizeof(int), protection, visibility, -1, 0);

            // m OR number of workers
            shmem[0] = atoi(argv[2]);

            //tempo_t OR total time
            shmem[1] = atoi(argv[3]);

            if(_num_workers > 0)
            {
                int *_workerPids = createWorkers(_num_workers, _size, writeMutex, shmem, shmemPath, _fileMatrix);

                //sleep(atoi(argv[3]));
                long _tEnd = _tBegin.tv_sec+atoi(argv[3]);
                while(_tNow.tv_sec < _tEnd){
                    gettimeofday(&_tNow, NULL);
                    //printf("[%lu][%lu]\n", _tNow.tv_sec, _tEnd);
                }
                //sleep(atoi(argv[3]));


                killWorkers(_workerPids, _num_workers);
                free(_workerPids);
                sem_destroy(&writeMutex);

                printf("\nind=[%d]  | n=[%d] | teste=[%s] | m=[%.0f] | tempo_t=[%.0f] | comp_res=[%.0f] | niter_res=[%.0f] | tempo_res = [%f]",
                (_numberOfTests+1),       _size,    argv[1],    shmem[0],     shmem[1],       shmem[2],          shmem[3],         shmem[4]);
                printf(" | resultado = [");
                for(i=0 ; i<_size ; i++ )
                {
                    printf("%d,",shmemPath[i]);
                }
                printf("\b] | WORKER_PID[%.0f]\n", shmem[7]);

                m_iterations += shmem[3];
                m_time += shmem[4];
            }

            freeMemory(_size, _fileMatrix);
            gettimeofday(&_tBegin, NULL);
            _NewPath = 0;
        }
        printf("\nM_Iterations=[%.0f] | M_Time=[%.03f]\n", (m_iterations/atoi(argv[4])), (m_time/atoi(argv[4])) );
    }
    else
    {

        
        _fileMatrix = readFile(argv[1], _fileMatrix, &_size);
       
        int _num_workers = atoi(argv[2]);

        // -1 is POSIX standard to run the shmem in memory instead of file
        shmem = mmap(NULL, _num_workers*(sizeof(int)), protection, visibility, -1, 0);
        shmemPath = mmap(NULL, _size*sizeof(int), protection, visibility, -1, 0);

        // m OR number of workers
        shmem[0] = atoi(argv[2]);

        //tempo_t OR total time
        shmem[1] = atoi(argv[3]);

        if(_num_workers > 0)
        {
            int *_workerPids = createWorkers(_num_workers, _size, writeMutex, shmem, shmemPath, _fileMatrix);

            long _tEnd = _tBegin.tv_sec+atoi(argv[3]);
            while(_tNow.tv_sec < _tEnd){
                gettimeofday(&_tNow, NULL);
                //printf("[%lu][%lu]\n", _tNow.tv_sec, _tEnd);
            }

            killWorkers(_workerPids, _num_workers);
            free(_workerPids);
            sem_destroy(&writeMutex);

            printf("\nind=[1]  | n=[%d] | teste=[%s] | m=[%.0f] | tempo_t=[%.0f] | comp_res=[%.0f] | niter_res=[%.0f] | tempo_res = [%f]",
            _size, argv[1], shmem[0], shmem[1], shmem[2], shmem[3], shmem[4]);
            printf(" | resultado = [");
            for(i=0 ; i<_size ; i++ )
            {
                printf("%d,",shmemPath[i]);
            }
            printf("\b] | WORKER_PID[%.0f]\n", shmem[7]);
            

        }
        freeMemory(_size, _fileMatrix);
    }
    return 0;
}

/*
________/\\\\\\\\\________/\\\________/\\\_______        
 _____/\\\////////________\/\\\_____/\\\//________       
  ___/\\\/_________________\/\\\__/\\\//___________      
   __/\\\___________________\/\\\\\\//\\\___________     
    _\/\\\___________________\/\\\//_\//\\\__________    
     _\//\\\__________________\/\\\____\//\\\_________   
      __\///\\\________________\/\\\_____\//\\\________  
       ____\////\\\\\\\\\__/\\\_\/\\\______\//\\\__/\\\_ 
        _______\/////////__\///__\///________\///__\///__
*/