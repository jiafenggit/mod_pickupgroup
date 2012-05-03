=============================
mod_pickupgroup Documentation
=============================

Description
===========

The intent of this module if of simplifying the usage of pickup groups on FreeSWITCH™ and circumvent some of the shortcomings of using the intercept application along with the channel variables and the hash application as described on the default configs.

The bigger shortcoming that motivated the development of this module is that the hash application is able to record a single UUID per group and therefore, the intercept application is able to pickup a single call within its group. If you have several calls and want to pick them up as a stack, the hash application won't support that.

Usage
=====

Channel Variables
-----------------

*pickupgroups*
    Can be a list of groups separated by a comma (,). On the following example, we show 2 ways of configuring the calls pickupgroups. One using the user directory and the other setting it on the dialplan directly.
    ::
    <extension name="Local_Extension">
         <condition field="destination_number" expression="^(10[01][0-9])$">
            <action application="export" data="dialed_extension=$1"/>
            <action application="bind_meta_app" data="1 b s execute_extension::dx XML features"/>
            <action application="bind_meta_app" data="2 b s record_session::$${recordings_dir}/${caller_id_number}.${strftime(%Y-%m-%d-%H-%M-%S)}.wav"/>
            <action application="bind_meta_app" data="3 b s execute_extension::cf XML features"/>
            <action application="bind_meta_app" data="4 b s execute_extension::att_xfer XML features"/>
            <action application="set" data="ringback=${us-ring}"/>
            <action application="set" data="transfer_ringback=$${hold_music}"/>
            <action application="set" data="call_timeout=30"/>
            <action application="set" data="hangup_after_bridge=true"/>
            <action application="set" data="continue_on_fail=true"/>
            <!--<action application="set" data="pickupgroups=${user_data(${dialed_extension}@${domain_name} var pickupgroups)}"/>-->
            <action application="set" data="pickupgroups=1,2,3"/>
            <action application="hash" data="insert/${domain_name}-call_return/${dialed_extension}/${caller_id_number}"/>
            <action application="hash" data="insert/${domain_name}-last_dial_ext/${dialed_extension}/${uuid}"/>
            <action application="set" data="called_party_callgroup=${user_data(${dialed_extension}@${domain_name} var callgroup)}"/>
            <action application="hash" data="insert/${domain_name}-last_dial_ext/${called_party_callgroup}/${uuid}"/>
            <action application="hash" data="insert/${domain_name}-last_dial_ext/global/${uuid}"/>
            <action application="hash" data="insert/${domain_name}-last_dial/${called_party_callgroup}/${uuid}"/>
            <action application="bridge" data="user/${dialed_extension}@${domain_name}"/>
            <action application="answer"/>
            <action application="sleep" data="1000"/>
            <action application="bridge" data="loopback/app=voicemail:default ${domain_name} ${dialed_extension}"/>
         </condition>
       </extension>


Dialplan Applications
---------------------

*pickupgroup*
    This application will look for the active channels for the specified comma separated groups to execute intercept on. Please note that the intercept channel variables still apply and if you don't want to pickup a bridged     call, you must set the proper variable.
    ::
    <extension name="Pickup Group">
        <condition>
            <action application="answer"/>
            <action application="set" data="intercept_unanswered_only=true"/>
            <action application="pickupgroup" data="1,2,3"/>
        </condition>
    </extension>
    