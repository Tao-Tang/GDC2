/*
 This file is a part of GDC software distributed under GNU GPL 2 licence.
 The homepage of the GDC project is http://sun.aei.polsl.pl/REFRESH/index.php?page=projects&project=gdc&subpage=about
 
 Authors: Sebastian Deorowicz, Agnieszka Danek, Marcin Niemiec
 
 Version: 2.0
 Date   : 2015-March-05
 */

#ifndef _MMGR_H
#define _MMGR_H

#include "defs.h"
#include <memory>
#include <condition_variable>
#include <mutex>
#include <iostream>

using namespace std;

//************************************************************************************************************
// CMemoryPool
//************************************************************************************************************
template<typename T> class CMemoryPool {
	int64_t total_size;
	int64_t part_size;
	int64_t n_parts_total;
	int64_t n_parts_free;

	uchar_t *buffer, *raw_buffer;
	uint32_t *stack;

	mutable mutex mtx;							// The mutex to synchronise on
	condition_variable cv;						// The condition to wait for

public:
// **********************************************************
	CMemoryPool(int64_t _total_size, int64_t _part_size)
	{
		raw_buffer = NULL;
		buffer = NULL;
		stack  = NULL;
		Prepare(_total_size, _part_size);
	}

// **********************************************************
	~CMemoryPool()
	{
		Release();
	}

// **********************************************************
	void Prepare(int64_t _total_size, int64_t _part_size)
	{
		Release();

		n_parts_total = _total_size / _part_size;
		part_size     = (_part_size + 15) / 16 * 16;			// to allow mapping pointer to int*
		n_parts_free  = n_parts_total;
	
		total_size = n_parts_total * part_size;

		raw_buffer = new uchar_t[total_size+64];
		buffer     = raw_buffer;
		while(((uint64_t) buffer) % 64)
			buffer++;

		stack = new uint32_t[n_parts_total];
		for(uint32_t i = 0; i < n_parts_total; ++i)
			stack[i] = i;
	}

// **********************************************************
	void Release(void)
	{
		if(raw_buffer)
			delete[] raw_buffer;
		raw_buffer = NULL;
		buffer     = NULL;

		if(stack)
			delete[] stack;
		stack = NULL;
	}

// **********************************************************
	int64_t GetPartSize()
	{
		return part_size;
	}

// **********************************************************
	int64_t GetAvailableParts()
	{
//		lock_guard<mutex> lck(mtx);

		return n_parts_free;
	}

// **********************************************************
	void Reserve(T* &part)
	{
		unique_lock<mutex> lck(mtx);
		cv.wait(lck, [this]{return n_parts_free > 0;});
			
		part = buffer + stack[--n_parts_free]*part_size;
	}

// **********************************************************
	void Reserve(T* &part1, T* &part2)
	{
		unique_lock<mutex> lck(mtx);
		cv.wait(lck, [this]{return n_parts_free > 1;});
			
		part1 = buffer + stack[--n_parts_free]*part_size;
		part2 = buffer + stack[--n_parts_free]*part_size;
	}

// **********************************************************
	void Free(T* part)
	{
		lock_guard<mutex> lck(mtx);

		stack[n_parts_free++] = (uint32_t) ((((uchar_t*) part) - buffer) / part_size);
//		if(n_parts_free == 1)
		if(n_parts_free <= 2)
			cv.notify_all();		
	}

// **********************************************************
	void Free(T* part1, T* part2)
	{
		lock_guard<mutex> lck(mtx);

		stack[n_parts_free++] = (uint32_t) ((((uchar_t*) part1) - buffer) / part_size);
		stack[n_parts_free++] = (uint32_t) ((((uchar_t*) part2) - buffer) / part_size);
		if(n_parts_free <= 2)
//		if(n_parts_free == 2)
			cv.notify_all();		
	}
};


//************************************************************************************************************
// CMemoryMonitor
//************************************************************************************************************
class CMemoryMonitor {
	uint64_t max_memory;
	uint64_t memory_in_use;

	mutable mutex mtx;								// The mutex to synchronise on
	condition_variable cv_memory_full;				// The condition to wait for

	void show_status()
	{
		cout << "Memory monitor: " << max_memory << "  " << memory_in_use << "\n";
	}

public:
// **********************************************************
	CMemoryMonitor(uint64_t _max_memory) 
	{
		lock_guard<mutex> lck(mtx);
		max_memory    = _max_memory;
		memory_in_use = 0;
	}

// **********************************************************
	~CMemoryMonitor() 
	{}

// **********************************************************
// Try to increase the amount of allocated memory
	void Increase(uint64_t n) 
	{
		unique_lock<mutex> lck(mtx);
//		show_status();
		cv_memory_full.wait(lck, [this, n]{return memory_in_use + n <= max_memory;});
		memory_in_use += n;
	}

// **********************************************************
// Try to increase the amount of allocated memory
// If no memory is accupied so far, completes even if the amount of requested memory is higher than the limit!
	void ForceIncrease(uint64_t n) 
	{
		unique_lock<mutex> lck(mtx);
//		show_status();
		cv_memory_full.wait(lck, [this, n]{return memory_in_use + n <= max_memory || memory_in_use == 0;});
		memory_in_use += n;
	}

// **********************************************************
// Decrease the amount of allocated memory
	void Decrease(uint64_t n) 
	{
		lock_guard<mutex> lck(mtx);
//		show_status();
		memory_in_use -= n;
		cv_memory_full.notify_all();
	}

// **********************************************************
// Return an info about the limit and the amount of allocated memory
	void Info(uint64_t &_max_memory, uint64_t &_memory_in_use)
	{
		lock_guard<mutex> lck(mtx);
		_max_memory    = max_memory;
		_memory_in_use = memory_in_use;
	}
};

#endif
