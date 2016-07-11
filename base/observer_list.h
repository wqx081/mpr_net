#ifndef BASE_OBSERVER_LIST_H_
#define BASE_OBSERVER_LIST_H_


// Example usage:
//
//   class MyConnection {
//    public:
//      ...
//
//      class Observer {
//       public:
//        virtual void OnFoo(MyConnection* w) = 0;
//        virtual void OnBar(MyConnection* w, int x, int y) = 0;
//      };
//
//      void AddObserver(Observer* obs) {
//        observer_list_.AddObserver(obs);
//      }
//
//      void RemoveObserver(Observer* obs) {
//        observer_list_.RemoveObserver(obs);
//      }
//
//    private:
//     base::ObserverLis<Observer> observer_list_;
//   };
//


#endif // OBSERVER_LIST_H_
