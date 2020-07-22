// Femc Driver
// Copyright (C) 2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#include "fdbuffer.hh"
#include "fdbuffer.icc"

extern "C" {
#include "../dispatcher.h"
#include "../error_stack.h"
}

#include <cerrno>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

//

using fdbuffer_t = FdBuffer<8000>;

// ------------------------------------------------------------

template< typename Arguments >
struct Node : std::enable_shared_from_this<Node<Arguments>> {
    virtual ~Node() = default;

    virtual void start(const Arguments&) {}

    std::shared_ptr<Node> self_owned;
};

//

template<>
struct Node< void > : std::enable_shared_from_this<Node<void>> {
    virtual ~Node() = default;

    virtual void start() {}

    std::shared_ptr<Node> self_owned;
};

// -----

template< typename Message,
          typename Arguments = void >
struct Slot : virtual Node<Arguments> {
    using slot_message_type = Message;

    //

    virtual void operator() (const slot_message_type& message) = 0;
};

//

template< typename Message,
          typename Arguments = void >
struct Signal : virtual Node<Arguments> {
    using signal_message_type = Message;

    //

    void emit(const signal_message_type& message)
    {
        for (auto& slot : m_slots)
        {
            (*slot)(message);
        }
    }

    void connect(std::shared_ptr< Slot<signal_message_type> >&& slot)
    {
        if (!slot) {
            throw std::runtime_error("Signal::connect(): slot is empty");
        }

        m_slots.push_back(std::move( slot ));
    }

private:
    std::vector< std::shared_ptr<Slot<signal_message_type>> > m_slots;
};

// ------------------------------------------------------------

template< typename ConcreteBlueprint,
          typename ConcreteNode,
          typename Arguments >
struct Blueprint : std::enable_shared_from_this<ConcreteBlueprint>
{
    Arguments m_args;

    void args(const Arguments& a) { m_args = a; }
    void args(Arguments&& a)      { m_args = a; }

    //

    virtual ~Blueprint() = default;
    virtual void createChildren(ConcreteNode&) {}

    std::shared_ptr<ConcreteNode> createNode()
    {
        std::shared_ptr<ConcreteNode> node = std::make_shared<ConcreteNode>();

        createChildren(*node);

        node->start(m_args);

        return node;
    }

    std::shared_ptr<ConcreteBlueprint> instance()
    {
        return std::make_shared<ConcreteBlueprint>(m_args);
    }
};

//

template< typename ConcreteBlueprint,
          typename ConcreteNode >
struct Blueprint< ConcreteBlueprint,
                  ConcreteNode,
                  void > : std::enable_shared_from_this<ConcreteBlueprint>
{
    virtual ~Blueprint() = default;
    virtual void createChildren(ConcreteNode&) {}

    std::shared_ptr<ConcreteNode> createNode()
    {
        std::shared_ptr<ConcreteNode> node = std::make_shared<ConcreteNode>();

        createChildren(*node);

        node->start();

        return node;
    }

    std::shared_ptr<ConcreteBlueprint> instance()
    {
        return std::make_shared<ConcreteBlueprint>();
    }
};

// -----

struct Blueprint_SlotBase {};
struct Blueprint_SignalBase {};

template< typename Message > struct Blueprint_SlotMsgType;
template< typename Message > struct Blueprint_SignalMsgType;

//

template< typename Message >
struct Blueprint_SignalMsgType : Blueprint_SignalBase {
    std::vector< std::shared_ptr<Blueprint_SlotMsgType<Message>> > m_slots;

    void connect(const std::shared_ptr<Blueprint_SlotMsgType<Message>>& slot)
    {
        m_slots.push_back(slot);
    }
};

template< typename Message >
struct Blueprint_SlotMsgType : Blueprint_SlotBase {
    virtual std::shared_ptr<Slot<Message>> createSlot() = 0;
};

//

template< typename ConcreteBlueprint,
          typename ConcreteNode,
          typename Arguments = void >
struct SlotBlueprint : Blueprint_SlotMsgType< typename ConcreteNode::slot_message_type >,
                       virtual Blueprint< ConcreteBlueprint,
                                          ConcreteNode,
                                          Arguments >
{
    using slot_message_type = typename ConcreteNode::slot_message_type;

    //

    std::shared_ptr<Slot<slot_message_type>> createSlot() override
    {
        return Blueprint< ConcreteBlueprint,
                          ConcreteNode,
                          Arguments >::createNode();
    }
};

// -----

template< typename ConcreteBlueprint,
          typename ConcreteNode,
          typename Arguments = void >
struct SignalBlueprint : Blueprint_SignalMsgType< typename ConcreteNode::signal_message_type >,
                         virtual Blueprint< ConcreteBlueprint,
                                            ConcreteNode,
                                            Arguments >
{
    using std::enable_shared_from_this<ConcreteBlueprint>::shared_from_this;
    //

    using signal_message_type = typename ConcreteNode::signal_message_type;

    //

    void createChildren(ConcreteNode& node) override
    {
        for (auto& slotPtr : this->m_slots)
        {
            node.connect(slotPtr->createSlot());
        }
    }
};

// ------------------------------------------------------------

struct pipeline_not_complete : std::logic_error {
    pipeline_not_complete()
        : logic_error("Executing incomplete pipeline") {}
};

// -----

template< typename HeadType,
          typename TailType >
struct Pipeline {
    using head_type = HeadType;
    using tail_type = TailType;

    std::shared_ptr<head_type> head;
    std::shared_ptr<tail_type> tail;

    //
};

// -----

namespace pipeline_tags
{
    struct no_error {};
}

// -----

template< typename PipelineType,
          typename std::enable_if< !(std::is_base_of< Blueprint_SlotBase, typename PipelineType::head_type >{}
                                     || std::is_base_of< Blueprint_SignalBase, typename PipelineType::tail_type >{}),
                                   int>::type = 0 >
void run(PipelineType& pipeline, pipeline_tags::no_error = {})
{
    pipeline.head->createNode();
}

template< typename PipelineType,
          typename std::enable_if< std::is_base_of< Blueprint_SlotBase, typename PipelineType::head_type >{}
                                   || std::is_base_of< Blueprint_SignalBase, typename PipelineType::tail_type >{},
                                   int>::type = 0 >
void run(PipelineType&)
{
    throw pipeline_not_complete();
}

template< typename PipelineType,
          typename std::enable_if< std::is_base_of< Blueprint_SlotBase, typename PipelineType::head_type >{}
                                   || std::is_base_of< Blueprint_SignalBase, typename PipelineType::tail_type >{},
                                   int>::type = 0 >
void run(PipelineType&, pipeline_tags::no_error)
{
}

// -----

template< typename SignalType,
          typename SlotType,
          std::enable_if_t<std::is_same< typename SignalType::signal_message_type,
                                         typename SlotType::slot_message_type >::value, int> = 0 >
Pipeline<SignalType, SlotType> operator| (SignalType signal, SlotType slot)
{
    Pipeline<SignalType, SlotType> pipeline{ signal.instance(),
                                             slot.instance() };

    pipeline.head->connect( pipeline.tail );

    run(pipeline, pipeline_tags::no_error());

    return pipeline;
}

template< typename PipelineType,
          typename SlotType,
          std::enable_if_t<std::is_same< typename PipelineType::tail_type::signal_message_type,
                                         typename SlotType::slot_message_type >::value, int> = 0 >
Pipeline<typename PipelineType::head_type, SlotType> operator| (PipelineType left, SlotType slot)
{
    auto oldTail = std::move( left.tail );

    Pipeline<typename PipelineType::head_type, SlotType> pipeline{ left.head,
                                                                   slot.instance() };
    oldTail->connect( pipeline.tail );

    run(pipeline, pipeline_tags::no_error());

    return pipeline;
}

template< typename SignalType,
          typename PipelineType,
          std::enable_if_t<std::is_same< typename SignalType::signal_message_type,
                                         typename PipelineType::head_type::slot_message_type >::value, int> = 0 >
Pipeline<SignalType, typename PipelineType::tail_type> operator| (SignalType signal, PipelineType right)
{
    auto oldHead = std::move( right.head );

    Pipeline<SignalType, typename PipelineType::tail_type> pipeline{ signal.instance(),
                                                                     right.tail };
    pipeline.head->connect( oldHead );

    run(pipeline, pipeline_tags::no_error());

    return pipeline;
}

// ------------------------------------------------------------

template< typename MessageType,
          typename PipelineType,
          std::enable_if_t<std::is_same< MessageType,
                                         typename PipelineType::head_type::slot_message_type >::value, int> = 0 >
void operator| (const MessageType& message,
                PipelineType pipeline)
{
    auto node = pipeline.head->createNode();
    (*node)( message );
}

template< typename MessageType,
          typename PipelineType,
          typename std::enable_if< !std::is_same< MessageType,
                                                  typename PipelineType::head_type::slot_message_type >{}
                                   && std::is_constructible< typename PipelineType::head_type::slot_message_type,
                                                             MessageType >{},
                                   int>::type = 0 >
void operator| (const MessageType& message,
                PipelineType pipeline)
{
    auto node = pipeline.head->createNode();
    (*node)( typename PipelineType::head_type::slot_message_type( message ) );
}

// ------------------------------------------------------------

// ----- NODES

namespace nodes
{
    struct say_stuff : Signal<std::string> {
        void start() override
        {
            if (self_owned) {
                throw std::logic_error("say_stuff: starting already started node\n");
            }
            self_owned = shared_from_this();

            //

            say();

            //

            fdd_add_timer([] (void* void_object, int)
                          {
                              if (say_stuff* object = static_cast<say_stuff*>( void_object );
                                  object)
                              {
                                  const bool cont = object->say();

                                  if (!cont) {
                                      object->self_owned.reset();

                                      fde_push_consistency_failure_id(fde_consistency_kill_recurring_timer);
                                      return false;
                                  }
                              }
                              return true;
                          },
                          this, 0, 1000, 1000);
        }

        bool say()
        {
            emit("stuff");

            --count;

            return count > 0;
        }

    private:
        unsigned int count = 3;
    };

    //

    struct center : Slot<std::string>,
                    Signal<std::string>
    {
        void operator() (const std::string& message) override
        {
            enum { LineWidth = 80 };

            const int leftShift = (LineWidth - message.size()) / 2;

            emit(std::string(leftShift, ' ') + message);
        }
    };

    //

    struct print : Slot<std::string> {
        void operator() (const std::string& message) override
        {
            std::cout << message << std::endl;
        }
    };

    //

    struct openfile : Signal<int, std::string> {
        void start(const std::string& filename) override
        {
            const int fd = ::open(filename.c_str(), O_RDONLY);

            if (fd >= 0) {
                emit(fd);
            }
            else {
                std::cerr << "openfile(" << filename << "): " << strerror(errno) << std::endl;
            }
        }
    };

    //

    struct readline : Slot<int>,
                      Signal<std::string>
    {
        readline()
        {
            fdd_init_service_input(&m_inputService,
                                   this,
                                   [] (void* void_object, int fd) -> bool
                                   {
                                       if (readline* object = static_cast<readline*>( void_object );
                                           object)
                                       {
                                           object->notifyInput(fd);
                                       }
                                       return true;
                                   });
        }

        void operator() (const int& fd) override
        {
            if (self_owned) {
                m_inputFds.push_back(fd);
            }
            else {
                self_owned = shared_from_this();
                fdd_add_input(fd, &m_inputService);
            }
        }

    private:
        fdd_service_input m_inputService;
        fdbuffer_t m_inputBuffer;
        std::vector<int> m_inputFds;

        //

        void notifyInput(int fd)
        {
            const int bytes = m_inputBuffer.read(fd);

            if (bytes > 0)
            {
                for (char* nl;
                     (nl =static_cast<char*>( memchr(m_inputBuffer.buffer, '\n', m_inputBuffer.filled) ));
                     )
                {
                    const ssize_t len = nl - m_inputBuffer.buffer;

                    if (len < 0) {
                        throw std::logic_error("memchr() returned pointer outside memory area");
                    }

                    emit( std::string(m_inputBuffer.buffer, len) );

                    m_inputBuffer.consume(len + 1);
                }

                if (m_inputBuffer.full())
                {
                    emit( std::string(m_inputBuffer.buffer, m_inputBuffer.filled) + '\\' );
                    m_inputBuffer.filled = 0;
                }
            }
            else {
                if (bytes < 0) {
                    std::cerr << "fd " << fd << ": " << strerror(errno) << std::endl;
                }

                fdd_remove_input(fd);
                close(fd);

                //

                if (!m_inputBuffer.empty()) {
                    emit( std::string(m_inputBuffer.buffer, m_inputBuffer.filled) + '\\' );
                    m_inputBuffer.filled = 0;
                }

                //

                if (m_inputFds.empty()) {
                    self_owned.reset();
                }
                else {
                    fdd_add_input(m_inputFds.front(), &m_inputService);
                    m_inputFds.erase(m_inputFds.begin());
                }
            }
        }
    };
}

// -----

namespace blueprints
{
    struct say_stuff : SignalBlueprint<say_stuff, nodes::say_stuff> {};

    struct center : SlotBlueprint<center, nodes::center>,
                    SignalBlueprint<center, nodes::center> {};

    struct print : SlotBlueprint<print, nodes::print> {};

    struct openfile : SignalBlueprint<openfile, nodes::openfile, std::string> {
        openfile(const std::string& a) { args(a); }
    };

    struct readline : SlotBlueprint<readline, nodes::readline>,
                      SignalBlueprint<readline, nodes::readline> {};

}

// -----

int main()
{
    using namespace blueprints;
    //

    try {
        auto centerPrint = center() | print();

        say_stuff() | centerPrint;

        "nasty stuff" | centerPrint;
        "more stuff" | centerPrint;

        //

        auto cat = openfile("file.txt") | readline();
        cat | print();

        //
        fdd_main(FDD_INFINITE);
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}
