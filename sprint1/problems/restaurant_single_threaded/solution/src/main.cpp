#ifdef WIN32
#include <sdkddkver.h>
#endif

#include <boost/asio.hpp>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <syncstream>
#include <unordered_map>
#include <memory>

namespace net = boost::asio;
namespace sys = boost::system;
namespace ph = std::placeholders;
using namespace std::chrono;
using namespace std::literals;
using Timer = net::steady_timer;

class Hamburger {
public:
    [[nodiscard]] bool IsCutletRoasted() const {
        return cutlet_roasted_;
    }
    void SetCutletRoasted() {
        if (IsCutletRoasted()) {  // Котлету можно жарить только один раз
            throw std::logic_error("Cutlet has been roasted already"s);
        }
        cutlet_roasted_ = true;
    }

    [[nodiscard]] bool HasOnion() const {
        return has_onion_;
    }
    // Добавляем лук
    void AddOnion() {
        if (IsPacked()) {  // Если гамбургер упакован, класть лук в него нельзя
            throw std::logic_error("Hamburger has been packed already"s);
        }
        AssureCutletRoasted();  // Лук разрешается класть лишь после прожаривания котлеты
        has_onion_ = true;
    }

    [[nodiscard]] bool IsPacked() const {
        return is_packed_;
    }
    void Pack() {
        AssureCutletRoasted();  // Нельзя упаковывать гамбургер, если котлета не прожарена
        is_packed_ = true;
    }

private:
    // Убеждаемся, что котлета прожарена
    void AssureCutletRoasted() const {
        if (!cutlet_roasted_) {
            throw std::logic_error("Bread has not been roasted yet"s);
        }
    }

    bool cutlet_roasted_ = false;  // Обжарена ли котлета?
    bool has_onion_ = false;       // Есть ли лук?
    bool is_packed_ = false;       // Упакован ли гамбургер?
};

std::ostream& operator<<(std::ostream& os, const Hamburger& h) {
    return os << "Hamburger: "sv << (h.IsCutletRoasted() ? "roasted cutlet"sv : " raw cutlet"sv)
              << (h.HasOnion() ? ", onion"sv : ""sv)
              << (h.IsPacked() ? ", packed"sv : ", not packed"sv);
}

class Logger {
public:
    explicit Logger(std::string id)
        : id_(std::move(id)) {
    }

    void LogMessage(std::string_view message) const {
        std::osyncstream os{std::cout};
        os << id_ << "> ["sv << duration<double>(steady_clock::now() - start_time_).count()
           << "s] "sv << message << std::endl;
    }

private:
    std::string id_;
    steady_clock::time_point start_time_{steady_clock::now()};
};

// Функция, которая будет вызвана по окончании обработки заказа
using OrderHandler = std::function<void(sys::error_code ec, int id, Hamburger* hamburger)>;

class Order : public std::enable_shared_from_this<Order> {
public:
    Order(net::io_context& ioc, int id, bool with_onion, OrderHandler handler)
            : ioc_{ioc}
            , id_{id}
            ,with_onion_{with_onion}
            ,handler_{std::move(handler)} {
    }

    void execute() {
        logger_.LogMessage("Order has been started."sv);
        roast_cutlet();
        if (with_onion_) {
            marinade_onion();
        }
    }
private:
    void roast_cutlet() {
        logger_.LogMessage("Start roasting cutlet."sv);
        roast_timer_.async_wait([self = shared_from_this()](sys::error_code ec) {
            self->on_roasted(ec);
        });
    }

    void on_roasted(sys::error_code ec) {
        if (ec) {
            logger_.LogMessage("Roast error: "s + ec.message());
        }
        else {
            logger_.LogMessage("On roasted"sv);
            hamburger_.SetCutletRoasted();
        }
        check_readiness(ec);
    }

    void marinade_onion() {
        logger_.LogMessage("Start marinading onion"sv);
        marinade_timer_.async_wait([self = shared_from_this()](sys::error_code ec) {
            self->on_onion_marinaded(ec);
        });
    }

    void on_onion_marinaded(sys::error_code ec) {
        if (ec) {
            logger_.LogMessage("Marinade onion error: " + ec.message());
        }
        else {
            logger_.LogMessage("Onion has been marinaded"sv);
            onion_marinaded_ = true;
        }
        check_readiness(ec);
    }

    void check_readiness(sys::error_code ec) {
        if (delivered_) {
            return;
        }
        if (ec) {
            return deliver(ec);
        }
        if (can_add_onion()) {
            logger_.LogMessage("Add onion"sv);
            hamburger_.AddOnion();
        }
        if (is_ready_to_pack()) {
            pack();
        }
    }

    void deliver(sys::error_code ec) {
        delivered_ = true;
        handler_(ec, id_, ec ? nullptr : &hamburger_);
    }

    [[nodiscard]] bool can_add_onion() const {
        return hamburger_.IsCutletRoasted() && onion_marinaded_ && !hamburger_.HasOnion();
    }

    [[nodiscard]] bool is_ready_to_pack() const {
        return hamburger_.IsCutletRoasted() && (!with_onion_ || hamburger_.HasOnion());
    }

    void pack() {
        logger_.LogMessage("Packing"sv);

        auto start = steady_clock::now();
        while (steady_clock::now() - start < 500ms) {
            // just wasting time
        }

        hamburger_.Pack();
        logger_.LogMessage("Packed"sv);

        deliver({});
    }

    net::io_context& ioc_;
    int id_;
    bool with_onion_;
    bool onion_marinaded_ = false;
    bool delivered_ = false;
    OrderHandler handler_;
    Hamburger hamburger_;
    Logger logger_ {std::to_string(id_)};
    Timer roast_timer_{ioc_, 1s};
    Timer marinade_timer_{ioc_, 2s};
};


class Restaurant {
public:
    explicit Restaurant(net::io_context& io)
        : io_(io) {
    }

    int MakeHamburger(bool with_onion, OrderHandler handler) {
        const int order_id = ++next_order_id_;
        /* Напишите недостающий код */
        std::make_shared<Order>(io_, order_id, with_onion, std::move(handler))->execute();
        return order_id;
    }

private:
    net::io_context& io_;
    int next_order_id_ = 0;
};

int main() {
    net::io_context io;

    Restaurant restaurant{io};

    Logger logger{"main"s};

    struct OrderResult {
        sys::error_code ec;
        Hamburger hamburger;
    };

    std::unordered_map<int, OrderResult> orders;
    auto handle_result = [&orders](sys::error_code ec, int id, Hamburger* h) {
        orders.emplace(id, OrderResult{ec, ec ? Hamburger{} : *h});
    };

    const int id1 = restaurant.MakeHamburger(false, handle_result);
    const int id2 = restaurant.MakeHamburger(true, handle_result);

    // До вызова io.run() никакие заказы не выполняются
    assert(orders.empty());
    io.run();

    // После вызова io.run() все заказы быть выполнены
    assert(orders.size() == 2u);
    {
        // Проверяем заказ без лука
        const auto& o = orders.at(id1);
        assert(!o.ec);
        assert(o.hamburger.IsCutletRoasted());
        assert(o.hamburger.IsPacked());
        assert(!o.hamburger.HasOnion());
    }
    {
        // Проверяем заказ с луком
        const auto& o = orders.at(id2);
        assert(!o.ec);
        assert(o.hamburger.IsCutletRoasted());
        assert(o.hamburger.IsPacked());
        assert(o.hamburger.HasOnion());
    }
}
