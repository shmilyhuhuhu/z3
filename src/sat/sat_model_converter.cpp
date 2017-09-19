/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    sat_model_converter.cpp

Abstract:

    Low level model converter for SAT solver.

Author:

    Leonardo de Moura (leonardo) 2011-05-26.

Revision History:

--*/
#include "sat/sat_model_converter.h"
#include "sat/sat_clause.h"
#include "util/trace.h"

namespace sat {

    model_converter::model_converter() {
    }

    model_converter::~model_converter() {
        reset();
    }

    void model_converter::reset() {
        m_entries.finalize();
    }

    model_converter& model_converter::operator=(model_converter const& other) {
        copy(other);
        return *this;
    }

    
    void model_converter::operator()(model & m) const {
        vector<entry>::const_iterator begin = m_entries.begin();
        vector<entry>::const_iterator it    = m_entries.end();
        while (it != begin) {
            --it;
            SASSERT(it->get_kind() != ELIM_VAR || m[it->var()] == l_undef);
            // if it->get_kind() == BLOCK_LIT, then it might be the case that m[it->var()] != l_undef,
            // and the following procedure flips its value.
            bool sat = false;
            bool var_sign = false;
            for (literal l : it->m_clauses) {
                if (l == null_literal) {
                    // end of clause
                    if (!sat) {
                        m[it->var()] = var_sign ? l_false : l_true;
                        break;
                    }
                    sat = false;
                    continue;
                }

                if (sat)
                    continue;
                bool sign  = l.sign();
                bool_var v = l.var();
                if (v == it->var())
                    var_sign = sign;
                if (value_at(l, m) == l_true)
                    sat = true;
                else if (!sat && v != it->var() && m[v] == l_undef) {
                    // clause can be satisfied by assigning v.
                    m[v] = sign ? l_false : l_true;
                    sat = true;
                }
            }
            DEBUG_CODE({
                // all clauses must be satisfied
                bool sat = false;
                for (literal l : it->m_clauses) {
                    if (l == null_literal) {
                        SASSERT(sat);
                        sat = false;
                        continue;
                    }
                    if (sat)
                        continue;
                    if (value_at(l, m) == l_true)
                        sat = true;
                }
            });
        }
    }

    /**
       \brief Test if after applying the model converter, all eliminated clauses are
       satisfied by m.
    */
    bool model_converter::check_model(model const & m) const {
        bool ok = true;
        vector<entry>::const_iterator begin = m_entries.begin();
        vector<entry>::const_iterator it    = m_entries.end();
        while (it != begin) {
            --it;
            bool sat = false;
            literal_vector::const_iterator it2     = it->m_clauses.begin();
            literal_vector::const_iterator itbegin = it2;
            literal_vector::const_iterator end2    = it->m_clauses.end();
            for (; it2 != end2; ++it2) {
                literal l  = *it2;
                if (l == null_literal) {
                    // end of clause
                    if (!sat) {
                        TRACE("sat_model_bug", tout << "failed eliminated: " << mk_lits_pp(static_cast<unsigned>(it2 - itbegin), itbegin) << "\n";);
                        ok = false;
                    }
                    sat = false;
                    itbegin = it2;
                    itbegin++;
                    continue;
                }
                if (sat)
                    continue;
                if (value_at(l, m) == l_true)
                    sat = true;
            }
        }
        return ok;
    }

    model_converter::entry & model_converter::mk(kind k, bool_var v) {
        m_entries.push_back(entry(k, v));
        entry & e = m_entries.back();
        SASSERT(e.var() == v);
        SASSERT(e.get_kind() == k);
        return e;
    }

    void model_converter::insert(entry & e, clause const & c) {
        SASSERT(c.contains(e.var()));
        SASSERT(m_entries.begin() <= &e);
        SASSERT(&e < m_entries.end());
        for (literal l : c) e.m_clauses.push_back(l);
        e.m_clauses.push_back(null_literal);
        TRACE("sat_mc_bug", tout << "adding: " << c << "\n";);
    }

    void model_converter::insert(entry & e, literal l1, literal l2) {
        SASSERT(l1.var() == e.var() || l2.var() == e.var());
        SASSERT(m_entries.begin() <= &e);
        SASSERT(&e < m_entries.end());
        e.m_clauses.push_back(l1);
        e.m_clauses.push_back(l2);
        e.m_clauses.push_back(null_literal);
        TRACE("sat_mc_bug", tout << "adding (binary): " << l1 << " " << l2 << "\n";);
    }

    void model_converter::insert(entry & e, clause_wrapper const & c) {
        SASSERT(c.contains(e.var()));
        SASSERT(m_entries.begin() <= &e);
        SASSERT(&e < m_entries.end());
        unsigned sz = c.size();
        for (unsigned i = 0; i < sz; ++i) 
            e.m_clauses.push_back(c[i]);
        e.m_clauses.push_back(null_literal);
        // TRACE("sat_mc_bug", tout << "adding (wrapper): "; for (literal l : c) tout << l << " "; tout << "\n";);
    }

    bool model_converter::check_invariant(unsigned num_vars) const {
        // After a variable v occurs in an entry n and the entry has kind ELIM_VAR,
        // then the variable must not occur in any other entry occurring after it.
        vector<entry>::const_iterator it  = m_entries.begin();
        vector<entry>::const_iterator end = m_entries.end();
        for (; it != end; ++it) {
            SASSERT(it->var() < num_vars);
            if (it->get_kind() == ELIM_VAR) {
                svector<entry>::const_iterator it2 = it;
                it2++;
                for (; it2 != end; ++it2) {
                    SASSERT(it2->var() != it->var());
                    for (literal l : it2->m_clauses) {
                        CTRACE("sat_model_converter", l.var() == it->var(), tout << "var: " << it->var() << "\n"; display(tout););
                        SASSERT(l.var() != it->var());
                        SASSERT(l == null_literal || l.var() < num_vars);
                    }
                }
            }
        }
        return true;
    }

    void model_converter::display(std::ostream & out) const {
        out << "(sat::model-converter";
        for (auto & entry : m_entries) {
            out << "\n  (" << (entry.get_kind() == ELIM_VAR ? "elim" : "blocked") << " " << entry.var();
            bool start = true;
            for (literal l : entry.m_clauses) {
                if (start) {
                    out << "\n    (";
                    start = false;
                }
                else {
                    if (l != null_literal)
                        out << " ";
                }
                if (l == null_literal) {
                    out << ")";
                    start = true;
                    continue;
                }
                out << l;
            }
            out << ")";
        }
        out << ")\n";
    }

    void model_converter::copy(model_converter const & src) {
        reset();
        m_entries.append(src.m_entries);
    }

    void model_converter::collect_vars(bool_var_set & s) const {
        for (entry const & e : m_entries) s.insert(e.m_var);
    }

    unsigned model_converter::max_var(unsigned min) const {
        unsigned result = min;
        vector<entry>::const_iterator it = m_entries.begin();
        vector<entry>::const_iterator end = m_entries.end();
        for (; it != end; ++it) {
            literal_vector::const_iterator lvit = it->m_clauses.begin();
            literal_vector::const_iterator lvend = it->m_clauses.end();
            for (; lvit != lvend; ++lvit) {
                literal l = *lvit;
                if (l != null_literal) {
                    if (l.var() > result)
                        result = l.var();
                }
            }
        }
        return result;
    }

};
