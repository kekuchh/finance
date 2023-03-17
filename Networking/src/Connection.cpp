//
// Created by Kekuch13 on 14.03.2023.
//

#include <Networking/Connection.h>

#include <boost/date_time.hpp>

Connection::Connection(net::io_context &ioc) : socket(ioc), dbManager() {}

std::shared_ptr<Connection> Connection::create(net::io_context &ioc) {
    return std::make_shared<Connection>(ioc);
}

tcp::socket &Connection::GetSocket() {
    return socket;
}

void Connection::Start() {
    beast::error_code ec;
    while (true) {
        buffer.clear();
        req.body().clear();
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
}

void Connection::sendResponse(http::message_generator &&res) {
    beast::error_code ec;
    beast::write(socket, std::move(res), ec);

    if (ec) {
        std::cerr << "Error on writing:" << ec.message();
    }
}

void Connection::handle_request() {
    std::cout << "----------\n" << req.method_string() << std::endl << req.target() << std::endl << req.body()
              << std::endl;

    switch (req.method()) {
        case http::verb::post:
            if (req.target() == "/accounts") {
                addAccount();
            } else if (req.target() == "/expenses") {
                addExpense();
            } else if (req.target() == "/income") {
                addIncome();
            } else if (req.target().starts_with("/categories")) {
                addCategory();
            } else {
                bad_request("Unknown path");
            }
            break;
        case http::verb::put:
            if (req.target() == "/accounts") {
                modifyAccount();
            } else if (req.target() == "/expenses") {
                modifyExpense();
            } else if (req.target() == "/income") {
                modifyIncome();
            } else if (req.target().starts_with("/categories")) {
                modifyCategory();
            } else {
                bad_request("Unknown path");
            }
            break;
        case http::verb::get:
            if (req.target().starts_with("/accounts")) {
                getAccount();
            } else if (req.target().starts_with("/expenses")) {
                getExpense();
            } else if (req.target().starts_with("/income")) {
                getIncome();
            } else if (req.target().starts_with("/categories")) {
                getCategory();
            } else {
                bad_request("Unknown path");
            }
            break;
        case http::verb::delete_:
            if (req.target().starts_with("/accounts")) {
                deleteAccount();
            } else if (req.target().starts_with("/expenses")) {
                deleteExpense();
            } else if (req.target().starts_with("/income")) {
                deleteIncome();
            } else if (req.target().starts_with("/categories")) {
                deleteCategory();
            } else {
                bad_request("Unknown path");
            }
            break;
        default:bad_request("Unknown HTTP-method");
            break;
    }
}

void Connection::bad_request(beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();

    sendResponse(std::move(res));
}

void Connection::server_error(beast::string_view what) {
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + std::string(what) + "'";
    res.prepare_payload();

    sendResponse(std::move(res));
}

void Connection::success_response(http::status status) {
    http::response<http::string_body> res(status, req.version());
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.prepare_payload();

    sendResponse(std::move(res));
}

void Connection::addAccount() {
    try {
        if (req.body().empty()) {
            throw std::exception("Request's body is empty");
        }

        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        pqxx::work worker(dbManager.GetConn());
        worker.exec_prepared("addAccount", root.get<std::string>("name"), root.get<int>("amount", 0));
        worker.commit();
        success_response(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::addExpense() {
    try {
        if (req.body().empty()) {
            throw std::exception("Request's body is empty");
        }

        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        boost::posix_time::ptime timeLocal = boost::posix_time::second_clock::local_time();
        std::string curDate = to_simple_string(timeLocal.date());
        std::string curTime = to_simple_string(timeLocal.time_of_day());

        if (!recordExists(root.get<int>("id_account"), "bank_accounts")) {
            throw std::exception("Account doesn't exist");
        }
        if (!recordExists(root.get<int>("id_cat"), "expense_categories")) {
            throw std::exception("Category doesn't exist");
        }

        pqxx::work worker(dbManager.GetConn());
        worker.exec_prepared("addExpense",
                             root.get<int>("id_cat"),
                             root.get<int>("id_account"),
                             root.get<int>("amount"),
                             root.get<std::string>("date", curDate),
                             root.get<std::string>("time", curTime),
                             root.get<std::string>("comment", ""));

        worker.exec_prepared("decreaseAccountAmount",
                             root.get<int>("amount"),
                             root.get<int>("id_account"));
        worker.commit();
        success_response(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::addIncome() {
    try {
        if (req.body().empty()) {
            throw std::exception("Request's body is empty");
        }

        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        if (!recordExists(root.get<int>("id_account"), "bank_accounts")) {
            throw std::exception("Account doesn't exist");
        }
        if (!recordExists(root.get<int>("id_cat"), "income_categories")) {
            throw std::exception("Category doesn't exist");
        }

        boost::posix_time::ptime timeLocal = boost::posix_time::second_clock::local_time();
        std::string curDate = to_simple_string(timeLocal.date());
        std::string curTime = to_simple_string(timeLocal.time_of_day());

        pqxx::work worker(dbManager.GetConn());
        worker.exec_prepared("addIncome",
                             root.get<int>("id_income_cat"),
                             root.get<int>("id_account"),
                             root.get<int>("amount"),
                             root.get<std::string>("date", curDate),
                             root.get<std::string>("time", curTime),
                             root.get<std::string>("comment", ""));

        worker.exec_prepared("increaseAccountAmount",
                             root.get<int>("amount"),
                             root.get<int>("id_account"));
        worker.commit();
        success_response(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::addCategory() {
    try {
        if (req.body().empty()) {
            throw std::exception("Request's body is empty");
        }

        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        pqxx::work worker(dbManager.GetConn());
        if (req.target() == "/categories/income") {
            worker.exec_prepared("addIncomeCategory", root.get<std::string>("name"));
        } else if (req.target() == "/categories/expenses") {
            worker.exec_prepared("addExpenseCategory", root.get<std::string>("name"));
        } else {
            throw std::exception("Unknown type of categories");
        }
        worker.commit();
        success_response(http::status::created);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::modifyAccount() {
    try {
        if (req.body().empty()) {
            throw std::exception("Request's body is empty");
        }

        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        pqxx::work worker(dbManager.GetConn());
        if (!recordExists(root.get<int>("id_account"), "bank_accounts")) {
            worker.exec_prepared("addAccount", root.get<std::string>("name"), root.get<int>("amount", 0));
        } else {
            pqxx::result res = worker.exec_prepared("findAccount", root.get<int>("id_account"));
            worker.exec_prepared("modifyAccount",
                                 root.get<std::string>("name"),
                                 root.get<int>("amount", res[0]["amount"].as<int>()),
                                 root.get<int>("id_account")
            );
        }
        worker.commit();
        success_response(http::status::ok);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::modifyExpense() {
    //to-do
}

void Connection::modifyIncome() {
    //to-do
}

void Connection::modifyCategory() {
    try {
        if (req.body().empty()) {
            throw std::exception("Request's body is empty");
        }

        std::stringstream jsonEncoded(req.body());
        boost::property_tree::ptree root;
        boost::property_tree::read_json(jsonEncoded, root);

        pqxx::work worker(dbManager.GetConn());
        if (req.target() == "/categories/income") {
            if (recordExists(root.get<int>("id_cat"), "income_categories")) {
                worker.exec_prepared("modifyIncomeCategory", root.get<std::string>("name"), root.get<int>("id_cat"));
            } else {
                worker.exec_prepared("addIncomeCategory", root.get<std::string>("name"));
            }
        } else if (req.target() == "/categories/expenses") {
            if (recordExists(root.get<int>("id_cat"), "expense_categories")) {
                worker.exec_prepared("modifyExpenseCategory", root.get<std::string>("name"), root.get<int>("id_cat"));
            } else {
                worker.exec_prepared("addExpenseCategory", root.get<std::string>("name"));
            }
        } else {
            throw std::exception("Unknown type of categories");
        }
        worker.commit();
        success_response(http::status::ok);
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::getAccount() {
    //to-do
}

void Connection::getExpense() {
    //to-do
}

void Connection::getIncome() {
    //to-do
}

void Connection::getCategory() {
    //to-do
}

void Connection::deleteAccount() {
    try {
        if (!req.target().starts_with("/accounts?=")) {
            throw std::exception("Incorrect query");
        }

        int id = parseID();
        if (!recordExists(id, "bank_accounts")) {
            throw std::exception("Account doesn't exist");
        }

        pqxx::work worker(dbManager.GetConn());
        worker.exec_prepared("deleteAccount", id);
        worker.commit();
        success_response(http::status::ok);
    } catch (boost::bad_lexical_cast &e) {
        bad_request("ID must be an integer");
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::deleteExpense() {
    try {
        if (!req.target().starts_with("/expenses?=")) {
            throw std::exception("Incorrect query");
        }

        int id = parseID();
        if (!recordExists(id, "expenses")) {
            throw std::exception("Expense doesn't exist");
        }

        pqxx::work worker(dbManager.GetConn());
        worker.exec_prepared("deleteExpense", id);
        worker.commit();
        success_response(http::status::ok);
    } catch (boost::bad_lexical_cast &e) {
        bad_request("ID must be an integer");
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::deleteIncome() {
    try {
        if (!req.target().starts_with("/income?=")) {
            throw std::exception("Incorrect query");
        }

        int id = parseID();
        if (!recordExists(id, "income")) {
            throw std::exception("Income doesn't exist");
        }

        pqxx::work worker(dbManager.GetConn());
        worker.exec_prepared("deleteIncome", id);
        worker.commit();
        success_response(http::status::ok);
    } catch (boost::bad_lexical_cast &e) {
        bad_request("ID must be an integer");
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

void Connection::deleteCategory() {
    try {
        if (!req.target().starts_with("/categories/expenses?=") && !req.target().starts_with("/categories/income?=")) {
            throw std::exception("Unknown type of categories");
        }

        int id = parseID();
        pqxx::work worker(dbManager.GetConn());
        if (req.target().starts_with("/categories/expenses?=")) {
            if (!recordExists(id, "expense_categories")) {
                throw std::exception("Category doesn't exist");
            }
            worker.exec_prepared("deleteExpenseCategory", id);
        } else if (req.target().starts_with("/categories/income?=")) {
            if (!recordExists(id, "income_categories")) {
                throw std::exception("Category doesn't exist");
            }
            worker.exec_prepared("deleteIncomeCategory", id);
        } else {
            throw std::exception("Unknown type of categories");
        }
        worker.commit();
        success_response(http::status::ok);
    } catch (boost::bad_lexical_cast &e) {
        bad_request("ID must be an integer");
    } catch (std::exception &e) {
        bad_request(e.what());
    }
}

int Connection::parseID() {
    auto pos = req.target().find("?id=");
    std::string tmp = req.target().substr(pos + 4);
    if (tmp.empty()) {
        throw std::exception("Incorrect query");
    }
    return boost::lexical_cast<int>(tmp);
}

bool Connection::recordExists(int id, std::string &&table) {
    try {
        pqxx::work worker(dbManager.GetConn());
        pqxx::result result;
        if (table == "income_categories") {
            result = worker.exec_prepared("findIncomeCategory", id);
        } else if (table == "expense_categories") {
            result = worker.exec_prepared("findExpenseCategory", id);
        } else if (table == "expenses") {
            result = worker.exec_prepared("findExpense", id);
        } else if (table == "income") {
            result = worker.exec_prepared("findIncome", id);
        } else {
            result = worker.exec_prepared("findAccount", id);
        }

        worker.commit();

        if (result.size() == 1) {
            return true;
        } else {
            return false;
        }
    } catch (std::exception &e) {
        std::cerr << e.what();
        return false;
    }
}
