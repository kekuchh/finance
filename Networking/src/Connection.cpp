//
// Created by Kekuch13 on 14.03.2023.
//

#include <Networking/Connection.h>

#include <boost/date_time.hpp>

Connection::Connection(net::io_context &ioc) : socket(ioc), dbManager() {};

std::shared_ptr<Connection> Connection::create(net::io_context &ioc) {
    return std::make_shared<Connection>(ioc);
};

tcp::socket &Connection::GetSocket() {
    return socket;
}
void Connection::Start() {
    beast::error_code ec;
    while (true) {
        http::read(socket, buffer, req, ec);
        if (ec == http::error::end_of_stream) {
            break;
        }
        if (ec) {
            std::cerr << "Fail on reading: " << ec.message() << std::endl;
            break;
        }
        handle_request();
    }
    socket.shutdown(tcp::socket::shutdown_send, ec);
    std::cout << "Connection closed\n";
};

void Connection::sendResponse(http::message_generator &&res) {
    beast::error_code ec;
    beast::write(socket, std::move(res), ec);

    if (ec)
        std::cerr << "Error on writing:" << ec.message();
};

void Connection::handle_request() {
    std::cout << "----------\n" << req.method_string() << std::endl << req.target() << std::endl;

    switch (req.method()) {
        case http::verb::post:
            if (req.target() == "/account") {
                addAccount();
            }
            if (req.target() == "/expenses") {
                addExpense();
            }
            if (req.target() == "/income") {
                addIncome();
            }
            if (req.target().starts_with("/categories")) {
                addCategory();
            }
            break;
        case http::verb::put:
            if (req.target() == "/account") {
                renameAccount();
            }
            if (req.target() == "/expenses") {
                modifyExpense();
            }
            if (req.target() == "/income") {
                modifyIncome();
            }
            if (req.target().starts_with("/categories")) {
                renameCategory();
            }
            break;
        case http::verb::get:
            if (req.target().starts_with("/account")) {
                getAccount();
            }
            if (req.target().starts_with("/expenses")) {
                getExpense();
            }
            if (req.target().starts_with("/income")) {
                getIncome();
            }
            if (req.target().starts_with("/categories")) {
                getCategory();
            }
            break;
        case http::verb::delete_:
            if (req.target().starts_with("/account")) {
                deleteAccount();
            }
            if (req.target().starts_with("/expenses")) {
                deleteExpense();
            }
            if (req.target().starts_with("/income")) {
                deleteIncome();
            }
            if (req.target().starts_with("/categories")) {
                deleteCategory();
            }
            break;
        default:bad_request("Unknown HTTP-method");
            break;
    }
};

void Connection::bad_request(beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();

    sendResponse(std::move(res));
};

void Connection::server_error(beast::string_view what) {
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + std::string(what) + "'";
    res.prepare_payload();

    sendResponse(std::move(res));
};

void Connection::success(http::status status) {
    http::response<http::string_body> res(status, req.version());
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "Success";
    res.prepare_payload();

    sendResponse(std::move(res));
};

void Connection::addAccount() {
    try {
        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        if (root.empty()) {
            throw std::exception("Request's body is empty");
        }

        pqxx::work worker(dbManager.GetConn());
        dbManager.GetConn().prepare("insert", "INSERT INTO bank_accounts (name) VALUES($1)");

        worker.exec_prepared("insert", root.get<std::string>("name"));
        worker.commit();

        success(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
};

void Connection::addExpense() {
    try {
        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        if (root.empty()) {
            throw std::exception("Request's body is empty");
        }
        boost::posix_time::ptime timeLocal = boost::posix_time::second_clock::local_time();
        std::string curDate = to_simple_string(timeLocal.date());
        std::string curTime = to_simple_string(timeLocal.time_of_day());

        pqxx::work worker(dbManager.GetConn());
        dbManager.GetConn().prepare("insert",
                                    "INSERT INTO expenses (id_expense_cat, id_account, amount, date, time, comment) VALUES($1, $2, $3, $4, $5, $6)");
        worker.exec_prepared("insert",
                             root.get<int>("id_expense_cat"),
                             root.get<int>("id_account"),
                             root.get<int>("amount"),
                             root.get<std::string>("date", curDate),
                             root.get<std::string>("time", curTime),
                             root.get<std::string>("comment", ""));

        dbManager.GetConn().prepare("update",
                                    "UPDATE bank_accounts SET amount=amount-$1 WHERE id_account=$2");
        worker.exec_prepared("update",
                             root.get<int>("amount"),
                             root.get<int>("id_account"));
        worker.commit();

        success(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
};

void Connection::addIncome() {
    try {
        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        if (root.empty()) {
            throw std::exception("Request's body is empty");
        }
        boost::posix_time::ptime timeLocal = boost::posix_time::second_clock::local_time();
        std::string curDate = to_simple_string(timeLocal.date());
        std::string curTime = to_simple_string(timeLocal.time_of_day());

        pqxx::work worker(dbManager.GetConn());
        dbManager.GetConn().prepare("insert",
                                    "INSERT INTO income (id_income_cat, id_account, amount, date, time, comment) VALUES($1, $2, $3, $4, $5, $6)");
        worker.exec_prepared("insert",
                             root.get<int>("id_income_cat"),
                             root.get<int>("id_account"),
                             root.get<int>("amount"),
                             root.get<std::string>("date", curDate),
                             root.get<std::string>("time", curTime),
                             root.get<std::string>("comment", ""));

        dbManager.GetConn().prepare("update",
                                    "UPDATE bank_accounts SET amount=amount+$1 WHERE id_account=$2");
        worker.exec_prepared("update",
                             root.get<int>("amount"),
                             root.get<int>("id_account"));
        worker.commit();

        success(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
};

void Connection::addCategory() {
    try {
        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        if (root.empty()) {
            throw std::exception("Request's body is empty");
        }

        std::string category;
        if (req.target().ends_with("income")) {
            dbManager.GetConn().prepare("insert", "INSERT INTO income_categories (name) VALUES($1)");
        } else if (req.target().ends_with("expenses")) {
            dbManager.GetConn().prepare("insert", "INSERT INTO expense_categories (name) VALUES($1)");
        } else {
            throw std::exception("Unknown type of categories");
        }

        pqxx::work worker(dbManager.GetConn());
        worker.exec_prepared("insert", root.get<std::string>("name"));
        worker.commit();

        success(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
};

void Connection::renameAccount() {
    try {
        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        if (root.empty()) {
            throw std::exception("Request's body is empty");
        }

        pqxx::work worker(dbManager.GetConn());
        dbManager.GetConn().prepare("update", "UPDATE bank_accounts SET name=$1 WHERE id_account=$2");
        worker.exec_prepared("update", root.get<std::string>("name"), root.get<int>("id_account"));
        worker.commit();

        success(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
};

void Connection::modifyExpense() {

};

void Connection::modifyIncome() {

};

void Connection::renameCategory() {

};

void Connection::getAccount() {

};

void Connection::getExpense() {

};

void Connection::getIncome() {

};

void Connection::getCategory() {

};

void Connection::deleteAccount() {

};

void Connection::deleteExpense() {

};

void Connection::deleteIncome() {

};

void Connection::deleteCategory() {

};







