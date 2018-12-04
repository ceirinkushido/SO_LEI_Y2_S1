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
int *randomizePath(int _pathIterationSize, int *_pathPointer)
{
    int i, _random1, _random2, _aux1, _aux2;

    _random1 = rand() % _pathIterationSize;
    _random2 = rand() % _pathIterationSize;
    while(_random1 == _random2)
    {
        _random1 = rand() % _pathIterationSize;
    }
    
    _aux1 = _pathPointer[_random1];
    _aux2 = _pathPointer[_random2];
    
    _pathPointer[_random1] = _aux2;
    _pathPointer[_random2] = _aux1;

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
    for (i = 0 ; i < _pathSize ; i++)
    {
        _path = randomizePath(_pathSize, _path);
    }
    return _path;
}

//Calculates the best distance
int calculateDistance(int *_pathToRun, int _sizeOfPath, int **_fileMatrix)
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

	return _currentDist;
}

int writeToMemory(int _sizeOfPath, int _bestDistCurrent,int *_pathToRun, int iterationCounter, double *shmem, int *shmemPath)
{
    int i;
    //comp_res OR Size of path
    shmem[2] = _bestDistCurrent;
    
    //niter_res OR Number of iterations untill result
    shmem[3] = iterationCounter;

    //TODO: tempo_res or time left before end of program
    double _timeToEnd = ( _tNow.tv_sec - _tBegin.tv_sec);
    _timeToEnd += (_tNow.tv_usec / 1000.0)/1000;
    
    shmem[4] = _timeToEnd;
    
    for(i=0 ; i < _sizeOfPath ; i++)
    {
        shmemPath[i] = _pathToRun[i];
    }
    shmem[7] = getpid();
    kill(getppid(), SIGUSR1);
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
    int i, _currentDist, _bestDist = 0;
    // Fork worker processes
    for (i=0; i<_num_workers; i++) {
        _workersPid[i] = fork();
        if(_workersPid[i] == 0)
        {
            gettimeofday(&_randSeed, NULL);
            srand(_randSeed.tv_sec*getpid());
            gettimeofday(&_tNow, NULL);

            int iterationCounter = 1;
            int *_realPath = createPath(_size);
            while(1)
            {
                if(_NewPath)
                {
                    _NewPath = 0;
                    for(i=0 ; i<_size ; i++)
                    {
                        _realPath[i]=shmemPath[i];
                    }
                }

                _currentDist = calculateDistance(_realPath, _size, _fileMatrix);
                if(_bestDist > _currentDist || _bestDist == 0)
                {
                    _bestDist = _currentDist;
                    gettimeofday(&_tNow, NULL);

                    sem_wait(&writeMutex);
                    if(shmem[2] > _bestDist || shmem[2]  == 0)
                    {
                        writeToMemory(_size, _bestDist, _realPath, iterationCounter, shmem, shmemPath);
                    }
                    else
                    {
                        _bestDist = *shmem;
                    }
                    sem_post(&writeMutex);
                }
                _realPath = randomizePath(_size, _realPath);
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
        kill(_workerArray[i], SIGKILL);
    }
}

//Handles wait for it... signals (shocking i know)
void signalHandler(int signum)
{
    if (signum == SIGUSR1)
    {
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
    int i, j, _numShuffles, _size, _sleepTimer = 500000;
    int *shmemPath;
    int **_fileMatrix;
    int protection = PROT_READ | PROT_WRITE;
    int visibility = MAP_ANONYMOUS | MAP_SHARED;
    //Initiate Synch Tools
    sem_t writeMutex;
    sem_init(&writeMutex, 0, 1);
    
    double *shmem;
    signal(SIGUSR1, signalHandler);
    signal(SIGUSR2, signalHandler);
    gettimeofday(&_tBegin, NULL);

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

        if(_num_workers > 0)
        {
            int *_workerPids = createWorkers(_num_workers, _size, writeMutex, shmem, shmemPath, _fileMatrix);
            long _tEnd = _tBegin.tv_sec+atoi(argv[3]);
            while(_tNow.tv_sec < _tEnd){
                usleep(_sleepTimer);
                gettimeofday(&_tNow, NULL);
            }

            killWorkers(_workerPids, _num_workers);
            free(_workerPids);
            sem_destroy(&writeMutex);

            //tempo_t OR total time
            gettimeofday(&_tNow, NULL);
            double _tRun = ( _tNow.tv_sec - _tBegin.tv_sec);
            _tRun += (_tNow.tv_usec / 1000.0)/1000;

            printf("\nind=[%d]  | n=[%d] | teste=[%s] | m=[%.0f] | tempo_t=[%f] | comp_res=[%.0f] | niter_res=[%.0f] | tempo_res = [%f]",
            (_numberOfTests+1),   _size,    argv[1],    shmem[0],     _tRun,       shmem[2],          shmem[3],         shmem[4]);
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
    if(argc > 4  && atoi(argv[4]) > 1)
    {
        printf("\nM_Iterations=[%.0f] | M_Time=[%.03f]\n", (m_iterations/atoi(argv[4])), (m_time/atoi(argv[4])) );
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