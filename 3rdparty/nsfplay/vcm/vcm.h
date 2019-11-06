#ifndef _VCM_H_
#define _VCM_H_
//
// Versatile Configuration Management Library
//
#include <stdexcept>
#include <string>
#include <map>
#include <deque>
#include <vector>
#include <list>
#include <set>

#include <windows.h> // for thread safety

namespace vcm
{
  // �l
  class Value
  {
  public:
    // ����
    std::string data;    // �l�c�ǂ�Ȍ^�ł���ɕ�����Ƃ��ĕۑ�
    bool update;         // �A�b�v�f�[�g���ꂽ�Ȃ�true

    // ����
    Value();
    Value(const char *);
    Value(const std::string &);
    Value(int);

    operator int () const ;
    int GetInt() const ;
    operator std::string () const ;
    operator const char *() const ;
    std::string GetStr() const ;

    Value &operator = ( int );
    void SetInt( int );
    Value &operator = ( const std::string & );
    void SetStr( const std::string & );
    Value &operator = ( const char * );
    void SetStr( const char * );
  };

  // �R���t�B�O���[�V�����{�̂̒�`
  class ObserverI
  {
  public:
    virtual void Notify(int)=0;
  };

  class Observable
  {
  protected:
    std::set<ObserverI *>oblist;
  public:
    virtual void AttachObserver(ObserverI *p)
    {
      oblist.insert(p);
    }

    virtual void DetachObserver(ObserverI *p)
    {
      oblist.erase(p);
    }

    virtual int GetObserverNum()
    {
      return (int)oblist.size();
    }

    virtual ObserverI *GetObserver()
    {
      if(oblist.size()!=0)
        return *oblist.begin();
      else
        return NULL;
    }

    void Notify(int id)
    {
      std::set<ObserverI *>::iterator it;
      for(it=oblist.begin();it!=oblist.end();it++)
        (*it)->Notify(id);
    }
  }; 


  class Configuration : public Observable
  {
  protected:
    std::map < std::string, Value > data;

    // thread safety
    HANDLE mutex;
    class MutexGuard {
      protected:
        Configuration *c;
      public:
        MutexGuard(Configuration* c_) { c = c_; ::WaitForSingleObject(c->mutex, INFINITE); }
        ~MutexGuard() { ::ReleaseMutex(c->mutex); }
    };

  public:
    // thread safety
    Configuration()
    {
      mutex = ::CreateMutex(NULL, false, NULL);
    }
    ~Configuration()
    {
      ::CloseHandle(mutex);
    }

    // �l��ǂށD������΃G���[�D
    Value &operator[]( const std::string id )
    {
      MutexGuard mg_(this);
      std::map< std::string, Value >::iterator it;
      if(( it = data.find( id ) )==data.end() )
        throw std::out_of_range( id );
      else
        return it->second;
    }
    // �l����� �d�����č�낤�Ƃ���� false ���A���Ď��s
    bool CreateValue( const std::string id, const Value &value )
    {
      MutexGuard mg_(this);
      std::map< std::string, Value >::iterator it;
      if(( it = data.find( id ) )!=data.end() )
        return false;
      else
      {
        data[id] = value;
        return true;
      }
    }
    // �l���Z�b�g����D������΍��D
    inline void SetValue( const std::string id, const Value &value )
    {
      MutexGuard mg_(this);
      data[id] = value;
    }
    // �l��ǂށD������Ύ����I�ɍ쐬�����D
    inline Value &GetValue( const std::string id )
    {
      MutexGuard mg_(this);
      return data[id];
    }
    inline void Clear()
    { 
      MutexGuard mg_(this);
      data.clear(); 
    }
    void Read( Configuration &src )
    {
      MutexGuard mg_(this);
      std::map<std::string, Value>::iterator it;
      for(it=data.begin();it!=data.end();it++)
        it->second = src[it->first];
    }
    void Write( Configuration &src )
    {
      MutexGuard mg_(this);
      std::map<std::string, Value>::iterator it;
      for(it=data.begin();it!=data.end();it++)
        src[it->first]=it->second;
    }
  };

} // namespace vcm

#include "vcm_ui.h"

#endif