// Copyright 2025 Fancapital Inc.  All rights reserved.
#include <vector>
#include <iostream>
#include <sstream>
#include <string>
#include <regex>
#include <filesystem>
#include "yaml-cpp/yaml.h"
#include "options.h"

namespace fs = std::filesystem;

namespace co {
std::string FlowControlConfig::ToString() const {
    std::stringstream ss;

    return ss.str();
}
bool MemBrokerOptions::IsFlowControlEnabled() const {
    return !disable_flow_control_ && !enable_query_only_ && !flow_controls_.empty();
}

std::shared_ptr<MemBrokerOptions> MemBrokerOptions::Load(const std::string& filename) {
    auto getStr = [&](const YAML::Node& node, const std::string& name) {
        try {
            return node[name] && !node[name].IsNull() ? node[name].as<std::string>() : "";
        } catch (std::exception& e) {
            LOG_ERROR << "load configuration failed: name：" << name << ", error：" << e.what();
            throw std::runtime_error(e.what());
        }
    };
    auto getStrings = [&](std::vector<std::string>* ret, const YAML::Node& node, const std::string& name, bool drop_empty = false) {
        try {
            if (node[name] && !node[name].IsNull()) {
                for (auto item : node[name]) {
                    std::string s = x::Trim(item.as<std::string>());
                    if (!drop_empty || !s.empty()) {
                        ret->emplace_back(s);
                    }
                }
            }
        } catch (std::exception& e) {
            LOG_ERROR << "load configuration failed: name：" << name << ", error：" << e.what();
            throw std::runtime_error(e.what());
        }
    };
    auto getInt = [&](const YAML::Node& node, const std::string& name, const int64_t& default_value = 0) {
        try {
            return node[name] && !node[name].IsNull() ? node[name].as<int64_t>() : default_value;
        } catch (std::exception& e) {
            LOG_ERROR << "load configuration failed: name：" << name << ", error：" << e.what();
            throw std::runtime_error(e.what());
        }
    };
    auto getBool = [&](const YAML::Node& node, const std::string& name) {
        try {
            return node[name] && !node[name].IsNull() ? node[name].as<bool>() : false;
        } catch (std::exception& e) {
            LOG_ERROR << "load configuration failed: name：" << name << ", error：" << e.what();
            throw std::runtime_error(e.what());
        }
    };
    auto opt = std::make_shared<MemBrokerOptions>();
    std::string _filename = filename.empty() ? "broker.yaml" : filename;
    auto file = x::FindFile(_filename);
    opt->log_opt_ = x::LoggingOptions::Load(file);
    x::InitializeLogging(*opt->log_opt_);
    YAML::Node root = YAML::LoadFile(file);
    auto broker = root["broker"];

    opt->request_timeout_ms_ = getInt(broker, "request_timeout_ms");
    if (opt->request_timeout_ms_ <= 0) {
        opt->request_timeout_ms_ = 10000;
    }
    opt->disable_flow_control_ = getBool(broker, "disable_flow_control");

    opt->enable_stock_short_selling_ = getBool(broker, "enable_stock_short_selling");
    opt->enable_query_only_ = getBool(broker, "enable_query_only");
    opt->query_asset_interval_ms_ = getInt(broker, "query_asset_interval_ms");
    opt->query_position_interval_ms_ = getInt(broker, "query_position_interval_ms");
    opt->query_knock_interval_ms_ = getInt(broker, "query_knock_interval_ms");
    opt->idle_sleep_ns_ = getInt(broker, "idle_sleep_ns");
    opt->cpu_affinity_ = getInt(broker, "cpu_affinity", -1);
    opt->node_name_ = getStr(broker, "node_name");
    opt->mem_dir_ = getStr(broker, "mem_dir");
    opt->mem_req_file_ = getStr(broker, "mem_req_file");
    opt->mem_rep_file_ = getStr(broker, "mem_rep_file");
    opt->req_stream_id_ = getInt(broker, "req_stream_id");
    opt->rep_stream_id_ = getInt(broker, "rep_stream_id");
    opt->aeron_channel_ = getStr(broker, "aeron_channel");
    return opt;
}

string MemBrokerOptions::ToString() {
    std::regex re("cluster_token=[^&a-zA-Z0-9]*");
    string gw = trade_gateway_;
    std::regex_replace(gw, re, "*");
    std::stringstream ss;
    ss << "broker:" << std::endl
        << "  enable_query_only: " << (enable_query_only_ ? "true" : "false") << std::endl
        << "  query_asset_interval_ms: " << query_asset_interval_ms_ << "ms" << std::endl
        << "  query_position_interval_ms: " << query_position_interval_ms_ << "ms" << std::endl
        << "  query_knock_interval_ms: " << query_knock_interval_ms_ << "ms" << std::endl
        << "  request_timeout_ms: " << request_timeout_ms_ << "ms" << std::endl
        << "  disable_flow_control: " << std::boolalpha << disable_flow_control_ << std::endl;
    if (flow_controls_.empty()) {
        ss << "  flow_control: []" << std::endl;
    } else {
        ss << "  flow_control:" << std::endl;
        for (auto& cfg: flow_controls_) {
            ss << "    - " << cfg->ToString() << std::endl;
        }
    }
    ss << "  batch_order_size: " << batch_order_size_ << std::endl
       << "  enable_stock_short_selling: " << std::boolalpha << enable_stock_short_selling_ << std::endl
       << "  idle_sleep_ns: " << idle_sleep_ns_ << "ns" << std::endl
       << "  cpu_affinity: " << cpu_affinity_ << std::endl
       << "  mem_dir: " << mem_dir_ << std::endl
       << "  mem_req_file: " << mem_req_file_ << std::endl
       << "  mem_rep_file: " << mem_rep_file_ << std::endl
       << "  req_stream_id: " << req_stream_id_ << std::endl
       << "  rep_stream_id: " << rep_stream_id_ << std::endl
       << "  aeron_channel: " << aeron_channel_ << std::endl
       << log_opt_->ToString();
    return ss.str();
}
}  // namespace co
