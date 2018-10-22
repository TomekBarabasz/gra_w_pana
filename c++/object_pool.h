#pragma once
#include <vector>
#include <set>

struct FreeObjectByDelete {
	template <typename T>
	void operator()(T* o) { delete o; }
};

struct FreeObjectByRelease {
	template <typename T>
	void operator()(T* o) { o->release(); }
};

template <typename T>
struct TrackAliveObjects
{
	void issue(T* o) {
		alive.insert(o);
	}
	void giveback(T*o) {
		alive.erase(o);
	}
	template <class FreeObject>
	void release() {
		FreeObject freePolicy;
		for (auto *o : alive) freePolicy(o);
	}
	int getNumAlive() { return alive.size(); }
	std::set<T*> alive;
};

template <typename T>
struct TrackAliveObjects_countOnly
{
	void issue(T* o)   { ++numAliveObjects; }
	void giveback(T*o) { --numAliveObjects; }
	template <class FreeObject>
	void release() {}
	int getNumAlive() { return numAliveObjects; }
	int numAliveObjects = 0;
};

template <typename T, 
		  class FreeObject = FreeObjectByDelete, 
		  template <typename> class TrackAlive = TrackAliveObjects_countOnly>
struct ObjectPool
{
	std::vector<T*>  objects;
	FreeObject		_freePolicy;
	TrackAlive<T>	_trackPolicy;

	template <typename...Args>
	T* alloc(Args...args)
	{
		T *obj;
		if (!objects.empty())
		{
			obj = objects.back(); 
			objects.pop_back();
		}else {
			obj = new T(std::forward<Args>(args)...);
		}
		_trackPolicy.issue(obj);
		return obj;
	}
	void free(T* obj)
	{
		_trackPolicy.giveback(obj);
		objects.push_back(obj);
	}
	void release()
	{
		for (auto o : objects) _freePolicy(o);
		objects.clear();
		_trackPolicy.release<FreeObject>();
	}
};
